"""Admission checks for Metal's independently reflected shader layout."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("ludus_shader_driver", ROOT / "cmake/shaders/compile_shader.py")
DRIVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRIVER)


class MetalReflectionTests(unittest.TestCase):
    def size(self, data):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "reflection.json"
            path.write_text(json.dumps(data))
            return DRIVER.metal_uniform_size(path)

    def fixture(self):
        return {"parameters": [{"binding": {"kind": "constantBuffer", "index": 0},
                "type": {"kind": "constantBuffer", "elementVarLayout": {"binding": {"size": 48}}}}]}

    def test_empty_uniform_and_actual_metal_binding_category(self):
        self.assertEqual(self.size({"parameters": []}), 0)
        self.assertEqual(self.size(self.fixture()), 48)
        data = self.fixture()
        data["parameters"][0]["binding"]["kind"] = "descriptorTableSlot"
        with self.assertRaisesRegex(RuntimeError, "buffer 0"):
            self.size(data)

    def test_capacity_boundaries(self):
        for size in (1, 48, 16384):
            data = self.fixture()
            data["parameters"][0]["type"]["elementVarLayout"]["binding"]["size"] = size
            self.assertEqual(self.size(data), size)
        for size in (0, 16385):
            data = self.fixture()
            data["parameters"][0]["type"]["elementVarLayout"]["binding"]["size"] = size
            with self.assertRaisesRegex(RuntimeError, "bounds"):
                self.size(data)

    def test_extra_resources_and_nonzero_bindings_are_rejected(self):
        data = self.fixture()
        data["parameters"].append(copy.deepcopy(data["parameters"][0]))
        with self.assertRaisesRegex(RuntimeError, "one uniform"):
            self.size(data)
        for changes in ({"index": 1}, {"space": 1}, {"kind": "texture"}):
            data = self.fixture()
            data["parameters"][0]["binding"].update(changes)
            with self.assertRaisesRegex(RuntimeError, "buffer 0"):
                self.size(data)

    def test_host_archives_are_independently_pinned(self):
        pin = json.loads((ROOT / "config/shader_toolchain.json").read_text())["slang"]
        self.assertEqual(pin["version"], "2026.1.2")
        self.assertEqual(set(pin["macos"]), {"arm64", "x86_64"})
        for architecture, asset in pin["macos"].items():
            self.assertRegex(asset["sha256"], r"^[0-9a-f]{64}$")
            self.assertIn("macos-" + ("aarch64" if architecture == "arm64" else architecture), asset["url"])
            self.assertNotEqual(asset["sha256"], pin["sha256"])


if __name__ == "__main__":
    unittest.main()
