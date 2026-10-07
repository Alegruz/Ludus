"""Exercise tampered inputs and stale cooks without network or host compilation."""
import hashlib
import io
import json
from pathlib import Path
import runpy
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
TOOL = runpy.run_path(str(ROOT / "scripts/luau-probe"))
GLOBALS = TOOL["verify"].__globals__


class IntegrityTests(unittest.TestCase):
    def test_modified_host_compiler_is_rejected_before_cooking(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            compiler = work / "host-compiler/luau-compile"
            compiler.parent.mkdir()
            compiler.write_bytes(b"modified compiler")
            pin = work / "pin.json"
            pin.write_bytes(b"pin")
            (work / "compiler-manifest.json").write_text(json.dumps({
                "pin_sha256": TOOL["sha"](b"pin"), "compiler_sha256": TOOL["sha"](b"old compiler")}))
            with patch.dict(GLOBALS, WORK=work, PIN_PATH=pin):
                with self.assertRaisesRegex(RuntimeError, "Modified host compiler"):
                    TOOL["cook"]()

    def test_modified_archive_is_rejected_before_extraction(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "luau.tar.gz").write_bytes(b"unexpected archive")
            with patch.dict(GLOBALS, WORK=work):
                with self.assertRaisesRegex(RuntimeError, "modified Luau archive"):
                    TOOL["source_entries"]()

    def test_archive_traversal_and_links_are_rejected(self):
        for filename, link in (("root/../../escaped", False), ("root/link", True)):
            with self.subTest(filename=filename), tempfile.TemporaryDirectory() as directory:
                work = Path(directory)
                archive = work / "luau.tar.gz"
                with tarfile.open(archive, "w:gz") as bundle:
                    entry = tarfile.TarInfo(filename)
                    if link:
                        entry.type = tarfile.SYMTYPE
                        entry.linkname = "/outside"
                        bundle.addfile(entry)
                    else:
                        entry.size = 1
                        bundle.addfile(entry, io.BytesIO(b"x"))
                pin = {"archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}
                with patch.dict(GLOBALS, WORK=work, PIN=pin):
                    with self.assertRaisesRegex(RuntimeError, "Unsafe|Unexpected"):
                        TOOL["source_entries"]()

    def test_source_modification_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / "vm.cpp").write_bytes(b"modified")
            with patch.dict(GLOBALS, SOURCE=source, source_entries=lambda: {"vm.cpp": b"pinned"}):
                with self.assertRaisesRegex(RuntimeError, "Modified Luau source"):
                    TOOL["verify"]()

    def test_changed_fixture_and_pin_invalidate_the_cook(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            cooked = work / "cooked"
            cooked.mkdir()
            fixtures = work / "fixtures"
            fixtures.mkdir()
            pin = work / "pin.json"
            pin.write_bytes(b"pin")
            (fixtures / "basic.luau").write_bytes(b"old")
            (cooked / "fixtures.h").write_bytes(b"header")
            evidence = {"pin_sha256": TOOL["sha"](b"pin"), "header_sha256": TOOL["sha"](b"header"),
                        "fixtures": {"basic.luau": {"source_sha256": TOOL["sha"](b"old")}}}
            (cooked / "manifest.json").write_text(json.dumps(evidence))
            with patch.dict(GLOBALS, WORK=work, FIXTURES=fixtures, PIN_PATH=pin, source_entries=lambda: {}):
                TOOL["verify"]()
                (fixtures / "basic.luau").write_bytes(b"new")
                with self.assertRaisesRegex(RuntimeError, "Stale bytecode fixtures"):
                    TOOL["verify"]()
                pin.write_bytes(b"new pin")
                with self.assertRaisesRegex(RuntimeError, "Stale bytecode pin"):
                    TOOL["verify"]()


if __name__ == "__main__":
    unittest.main()
