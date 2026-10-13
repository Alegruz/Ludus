"""Byte-exact regression of the Python scene/texture pipeline against Phase 0 goldens.

Run: python3 -m unittest discover -s tests/goldens/scene_pipeline -v
A failure means the observable bytes changed. That is only acceptable as a deliberate,
reviewed recapture (`capture.py --write`), because a later C++ port is verified against
these same goldens.
"""
import json
import os
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture  # noqa: E402


def _captured_float_sum():
    return json.loads((capture.EXPECTED / "goldens.json").read_text())["normalization"]["float_sum"]


@unittest.skipUnless(capture.float_sum_semantics() == _captured_float_sum(),
                     "goldens need Python >= 3.12 compensated float sum(); see README")
class SceneGoldenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.goldens, cls.files = capture.capture()
        cls.expected = json.loads((capture.EXPECTED / "goldens.json").read_text())

    def test_fixture_generator_matches_committed_source(self):
        self.assertEqual(self.expected["source"], self.goldens["source"])
        self.assertEqual(capture.tree(capture.SOURCE), self.goldens["source"])

    def test_python_files_are_the_captured_revision(self):
        self.assertEqual(self.expected["python_files"], self.goldens["python_files"],
                         "scene/texture Python changed; recapture goldens deliberately")

    def test_every_output_is_byte_identical(self):
        self.assertEqual(sorted(self.expected["outputs"]), sorted(self.goldens["outputs"]))
        for name, record in self.expected["outputs"].items():
            with self.subTest(name=name):
                self.assertEqual(record, self.goldens["outputs"][name])
                if record["inline"]:
                    self.assertEqual((capture.EXPECTED / "files" / name).read_bytes(), self.files[name])

    def test_capture_is_deterministic(self):
        again, _ = capture.capture()
        self.assertEqual(self.goldens, again)

    def test_published_release_matches_project_closure(self):
        outputs = self.goldens["outputs"]
        for name, record in outputs.items():
            if name.startswith("release/assets/objects/"):
                self.assertEqual(record, outputs["project/" + name[len("release/"):]])
        self.assertEqual(outputs["release/scene.luscn"], outputs["assemble/main.luscn"])


@unittest.skipUnless(os.environ.get("LUDUS_STAIRCASE2_DIR"), "set LUDUS_STAIRCASE2_DIR to the licensed scene")
class LicensedStaircase2Tests(unittest.TestCase):
    def test_digests(self):
        record = json.loads((capture.HERE / "licensed/staircase2.json").read_text())
        if record.get("outputs") is None:
            self.skipTest("licensed/staircase2.json not captured yet")
        self.assertEqual(0, capture.licensed(os.environ["LUDUS_STAIRCASE2_DIR"], False))


if __name__ == "__main__":
    unittest.main()
