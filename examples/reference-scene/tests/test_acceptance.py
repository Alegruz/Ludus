"""Reject false-positive renderer acceptance independently of GPU availability."""

import copy
import json
from pathlib import Path
import struct
import subprocess
import shutil
import tempfile
import unittest
from unittest.mock import patch

import acceptance
from check_capture import fixture


class AcceptanceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.binary = self.root / "renderer"
        self.binary.write_bytes(b"test renderer")
        (self.root / "adapter-name.txt").write_text("synthetic adapter")
        self.counts = acceptance.bundle_counts(fixture())
        (self.root / "scene.luscn").write_bytes(fixture())
        self.evidence = b"LUSAC01\0" + struct.pack("<16Q", *self.counts, 1, 1, 1, 1, 0, 1, 3, 1, 1)
        (self.root / "scene-acceptance.bin").write_bytes(self.evidence)
        (self.root / "identity-acceptance.bin").write_bytes(self.evidence)
        for name in ("raster-reference.rgba", "raster-optimized.rgba"):
            (self.root / name).write_bytes(bytes([128, 128, 128, 255]))
        for name in ("identity-reference.rgba", "identity-optimized.rgba"):
            (self.root / name).write_bytes(bytes([1, 74, 152, 255]))
        data = bytearray(256)
        data[:8] = b"LUHDR01\0"
        struct.pack_into("<10I", data, 8, 1, 1, 1, 256, 1, 1, 2, 4, 3, 1)
        struct.pack_into("<4Q", data, 48, 871, 1, 12, 34)
        data += struct.pack("<fffI", .5, .5, .5, 2)
        self.capture = data
        (self.root / "capture.luhdr").write_bytes(data)

    def verify(self):
        return acceptance.verify_run(self.root, self.counts, 1, 2)

    def test_valid_evidence(self):
        result, _ = self.verify()
        self.assertEqual(1, result["submitted"])
        self.assertEqual(0, result["raster_max_byte_error"])
        self.assertEqual(8, len(result["artifact_sha256"]))
        self.assertEqual([1], result["observed_source_ids"])

    def test_foreign_identity_pixels_fail_with_matching_lit_images(self):
        for name in ("identity-reference.rgba", "identity-optimized.rgba"):
            (self.root / name).write_bytes(bytes([255, 0, 255, 255]))
        with self.assertRaisesRegex(ValueError, "foreign/unsubmitted"):
            self.verify()

    def test_missing_geometry_and_identity_fail(self):
        for offset, value in ((8 + 6 * 8, 1), (120, 2), (128, 0), (8 + 7 * 8, 0)):
            with self.subTest(offset=offset):
                bad = bytearray(self.evidence)
                struct.pack_into("<Q", bad, offset, value)
                (self.root / "scene-acceptance.bin").write_bytes(bad)
                with self.assertRaises(ValueError):
                    self.verify()

    def test_truncated_or_trailing_evidence_fails(self):
        for data in (self.evidence[:-1], self.evidence + b"x"):
            (self.root / "scene-acceptance.bin").write_bytes(data)
            with self.assertRaises(ValueError):
                self.verify()

    def test_changed_raster_or_black_framing_fails(self):
        (self.root / "raster-optimized.rgba").write_bytes(bytes([125, 128, 128, 255]))
        with self.assertRaisesRegex(ValueError, "differs"):
            self.verify()
        for name in ("raster-reference.rgba", "raster-optimized.rgba"):
            (self.root / name).write_bytes(bytes([0, 0, 0, 255]))
        with self.assertRaisesRegex(ValueError, "framing"):
            self.verify()

    def test_incomplete_samples_and_wrong_traversal_fail(self):
        for field, value in ((len(self.capture) - 4, 1), (44, 0), (44, 33)):
            data = bytearray(self.capture)
            struct.pack_into("<I", data, field, value)
            (self.root / "capture.luhdr").write_bytes(data)
            with self.assertRaises(ValueError):
                self.verify()

    def test_native_parity_requires_matching_contract_and_pixels(self):
        cpu = self.verify()
        native = copy.deepcopy(cpu)
        native[0]["capture"]["traversal"] = "native_queries"
        self.assertEqual(0, acceptance.compare_native(cpu, native)["max_rgb_vector_error"])
        native[0]["capture"]["seed"] += 1
        with self.assertRaisesRegex(ValueError, "contract mismatch"):
            acceptance.compare_native(cpu, native)
        native = copy.deepcopy(cpu)
        native[1][0] = (.1, .5, .5, 2)
        with self.assertRaisesRegex(ValueError, "radiance error"):
            acceptance.compare_native(cpu, native)

    def test_bundle_walk_rejects_incomplete_or_trailing_data(self):
        for data in (fixture()[:-1], fixture() + b"x"):
            with self.assertRaises(ValueError):
                acceptance.bundle_counts(data)

    def test_manifest_mismatch_never_launches_renderer(self):
        (self.root / "manifest.json").write_text(json.dumps({"bundle_sha256": "wrong"}))
        with patch.object(acceptance.subprocess, "run") as launch:
            with self.assertRaisesRegex(ValueError, "manifest mismatch"):
                acceptance.run(self.root / "missing", self.root, self.root / "output")
            launch.assert_not_called()

    def test_failure_preserves_previous_acceptance_report(self):
        output = self.root / "output"
        output.mkdir()
        report = output / "acceptance.json"
        report.write_bytes(b"previous accepted report")
        with patch.object(acceptance.subprocess, "run", side_effect=subprocess.CalledProcessError(15, "renderer")):
            with self.assertRaises(subprocess.CalledProcessError):
                acceptance.run(self.binary, self.root, output)
        self.assertEqual(b"previous accepted report", report.read_bytes())
        failed = list(output.glob("attempt-*/failure.txt"))
        self.assertEqual(1, len(failed))
        self.assertIn("exit status 15", failed[0].read_text())

    def test_native_failure_retains_pixels_and_metrics_without_accepting(self):
        output = self.root / "native-failure"
        output.mkdir()
        report = output / "acceptance.json"
        report.write_bytes(b"previous report")

        def launch(command, **_):
            destination = Path(command[2])
            for name in ("scene-acceptance.bin", "identity-acceptance.bin", "raster-reference.rgba",
                         "raster-optimized.rgba", "identity-reference.rgba", "identity-optimized.rgba",
                         "adapter-name.txt", "capture.luhdr"):
                shutil.copyfile(self.root / name, destination / name)
            if command[-1] == "native":
                data = bytearray(self.capture)
                struct.pack_into("<I", data, 44, 0)
                struct.pack_into("<f", data, 256, .1)
                (destination / "capture.luhdr").write_bytes(data)

        with patch.object(acceptance.subprocess, "run", side_effect=launch):
            with self.assertRaisesRegex(ValueError, "radiance error"):
                acceptance.run(self.binary, self.root, output, 1, 2, True)
        self.assertEqual(b"previous report", report.read_bytes())
        failed = next(output.glob("attempt-*/native-parity.json"))
        metrics = json.loads(failed.read_text())
        self.assertFalse(metrics["passed"])
        self.assertEqual(1, metrics["failing_pixels"])
        self.assertTrue((failed.parent / "native/capture.luhdr").is_file())
        self.assertTrue((failed.parent / "cpu/run.json").is_file())


if __name__ == "__main__":
    unittest.main()
