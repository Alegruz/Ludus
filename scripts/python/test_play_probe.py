"""Artifact bounds and real owned-process Query failure isolation."""
from __future__ import annotations
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

from play_probe import ModuleProbe, ProbeError, debugger_stopped, elf_identity, validate_metadata

METADATA = dict(sdk_identity="sdk", abi_major=1, abi_minor=0, capabilities=3,
                property_schema=1, checkpoint_schema=1)


def fixture_elf() -> bytes:
    # Minimal ELF64 with a GNU ID and an embedded debug section. The parser
    # tests never execute it; process-isolation tests use real Python children.
    names = b"\0.shstrtab\0.debug_info\0"
    note = struct.pack("<III", 4, 8, 3) + b"GNU\0" + b"12345678"
    header = struct.pack("<16sHHIQQQIHHHHHH", b"\x7fELF\x02\x01\x01" + b"\0"*9,
                         3, 62, 1, 0, 64, 120, 0, 64, 56, 1, 64, 3, 1)
    program = struct.pack("<IIQQQQQQ", 4, 0, 312, 0, 0, len(note), len(note), 4)
    sections = b"\0"*64 + struct.pack("<IIQQQQIIQQ", 1, 3, 0, 0, 336, len(names), 0, 0, 1, 0)
    sections += struct.pack("<IIQQQQIIQQ", 11, 1, 0, 0, 336+len(names), 1, 0, 0, 1, 0)
    return header + program + sections + note + names + b"x"


class ProbeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.lease = (self.root / "lease").open("w+b")
        self.addCleanup(self.lease.close)

    def probe(self, source, **kwargs):
        host = self.root / "host"
        host.write_text(f"#!{sys.executable}\n" + source)
        host.chmod(0o755)
        probe = ModuleProbe(host, self.root / "module", cwd=self.root, env=dict(os.environ),
                            lease_fd=self.lease.fileno(), **kwargs)
        self.addCleanup(probe.cancel)
        return probe

    def finish(self, probe):
        deadline = time.monotonic()+8
        while time.monotonic() < deadline:
            result = probe.poll()
            if result is not None:
                return result
            # Synchronization polls an OS process, independent of build timing.
            time.sleep(0.005)
        self.fail("probe did not reach a bounded outcome")

    def test_script_debug_requires_abi_1_1(self):
        self.assertEqual(validate_metadata(dict(METADATA, abi_minor=1, capabilities=11))["capabilities"], 11)
        for fields in ({"abi_minor": 2}, {"capabilities": 16}, {"capabilities": 11}):
            with self.subTest(fields=fields), self.assertRaises(ProbeError):
                validate_metadata(dict(METADATA, **fields))

    def test_embedded_symbols_id_and_malformed_offsets(self):
        path = self.root / "artifact"
        original = fixture_elf()
        path.write_bytes(original)
        self.assertEqual(elf_identity(path), dict(build_id=b"12345678".hex(), embedded_symbols=True, elf_type=3))
        for offset, encoded in ((5, b"\2"), (40, struct.pack("<Q", 2**64-1)),
                                (312, struct.pack("<I", 2**32-1)), (376, b"")):
            data = bytearray(original)
            if encoded:
                data[offset:offset+len(encoded)] = encoded
            else:
                data = data[:offset-30]
            path.write_bytes(data)
            with self.subTest(offset=offset), self.assertRaises(ProbeError):
                elf_identity(path)
        path.write_bytes(original.replace(b".debug_info", b".other_info"))
        with self.assertRaisesRegex(ProbeError, "debug information"):
            elf_identity(path)
        self.assertFalse(elf_identity(path, require_symbols=False)["embedded_symbols"])

    def test_query_is_exact_and_closes_owned_process(self):
        probe = self.probe("import json\nprint(" + repr(json.dumps(METADATA)) + ")\n")
        self.assertEqual(self.finish(probe), METADATA)
        self.assertTrue(probe.cleanup_confirmed)
        self.assertTrue(probe.cancel())

    def test_malformed_mismatch_crash_and_output_flood(self):
        sources = ("print('{\"sdk_identity\":\"a\",\"sdk_identity\":\"b\"}')\n",
                   "print("+repr(json.dumps(METADATA))+ ")\n",
                   "import os,signal\nos.kill(os.getpid(),signal.SIGABRT)\n",
                   "import os\nos.write(1,b'x'*70000)\n")
        for index, source in enumerate(sources):
            with self.subTest(index=index):
                probe = self.probe(source, expected={**METADATA, "checkpoint_schema": 2})
                with self.assertRaises(ProbeError) as error:
                    self.finish(probe)
                self.assertTrue(error.exception.cleanup_confirmed)
                self.assertTrue(probe.finished)

    def test_hang_cancel_and_debugger_stop_does_not_expire_budget(self):
        probe = self.probe("import time\ntime.sleep(60)\n", timeout=0.03)
        with patch("play_probe.debugger_stopped", return_value=True):
            probe.last_poll = time.monotonic()-100
            self.assertIsNone(probe.poll())
            self.assertGreater(probe.remaining, 0)
        with self.assertRaisesRegex(ProbeError, "timed out"):
            self.finish(probe)
        self.assertTrue(probe.cleanup_confirmed)
        cancelled = self.probe("import time\ntime.sleep(60)\n")
        self.assertTrue(cancelled.cancel())
        self.assertTrue(cancelled.cancel())

    def test_closed_typed_metadata(self):
        for changes in ({"extra":0}, {"capabilities":8}, {"abi_major":True},
                        {"sdk_identity":"x\0"}, {"property_schema":0}):
            with self.subTest(changes=changes), self.assertRaises(ProbeError):
                validate_metadata({**METADATA, **changes})

    def test_debugger_stop_in_worker_blocks_even_when_leader_runs(self):
        tasks = self.root / "task"
        (tasks / "1").mkdir(parents=True)
        (tasks / "2").mkdir()
        (tasks / "1/status").write_text("State:\tR (running)\nTracerPid:\t7\n")
        worker = tasks / "2/status"
        worker.write_text("State:\tt (tracing stop)\nTracerPid:\t7\n")
        with patch("play_probe.Path", return_value=tasks):
            self.assertTrue(debugger_stopped(123))
            worker.write_text("State:\tS (sleeping)\nTracerPid:\t7\n")
            self.assertFalse(debugger_stopped(123))


if __name__ == "__main__":
    unittest.main()
