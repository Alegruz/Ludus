"""Byte-exact regression of cmake/shaders/cook_texture.py against the Phase 0 texture goldens.

Run: python3 -m unittest discover -s tests/goldens/scene_pipeline -p test_texture_goldens.py -v
This stays until the texture cooker is ported to C++; the C++ port is then checked against the
same files under expected/files/texture/.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
EXPECTED = HERE / "expected" / "files" / "texture"
TEXTURE_SOURCES = {
    "l3_color": ROOT / "tests/sdk_consumer/l3-color.texture.json",
    "l4_bake": ROOT / "tests/sdk_consumer/l4-bake.texture.json",
}
TEXTURE_SYNTHETIC = {
    "srgb_color_odd": {"width": 5, "height": 3, "format": "Rgba8Srgb", "semantic": "color",
                       "pixels": [(i * 37) % 256 for i in range(5 * 3 * 4)]},
    "rg_data": {"width": 3, "height": 2, "format": "Rg8Unorm", "semantic": "data",
                "pixels": [0, 255, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170]},
    "normal": {"width": 2, "height": 2, "format": "Rgba8Unorm", "semantic": "normal",
               "pixels": [255, 128, 128, 255, 128, 255, 128, 255, 0, 128, 128, 255, 128, 0, 128, 255]},
    "mask": {"width": 4, "height": 2, "format": "R8Unorm", "semantic": "mask", "cutoff": 0.5,
             "pixels": [255, 255, 255, 0, 0, 128, 200, 30]},
}
# Generated headers are stored with a ".golden" suffix so formatters and header checks skip them.
GOLDEN_SUFFIX = ".golden"


def read_tree(root):
    """Every regular file below root: relative path (without ".golden") -> bytes."""
    files = {}
    for path in sorted(Path(root).rglob("*")):
        if path.is_file() and "__pycache__" not in path.parts:
            name = path.relative_to(root).as_posix()
            files[name[: -len(GOLDEN_SUFFIX)] if name.endswith(GOLDEN_SUFFIX) else name] = path.read_bytes()
    return files


def cook_all(work):
    files = {}
    cases = dict(TEXTURE_SOURCES)
    for name, document in TEXTURE_SYNTHETIC.items():
        path = work / f"{name}.texture.json"
        path.write_text(json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n")
        cases[name] = path
    for name, path in sorted(cases.items()):
        out = work / "out" / name
        subprocess.run([sys.executable, str(ROOT / "cmake/shaders/cook_texture.py"), "--source", str(path),
                        "--name", name, "--output", str(out)], check=True)
        for relative, data in read_tree(out).items():
            files[f"{name}/{relative}"] = data
        if name in TEXTURE_SYNTHETIC:
            files[f"{name}/source.texture.json"] = path.read_bytes()
    return files


@unittest.skipUnless(sum([1e16, 1.0, -1e16]) == 1.0,
                     "goldens need Python >= 3.12 compensated float sum(); see README")
class TextureGoldenTests(unittest.TestCase):
    def test_every_texture_output_is_byte_identical(self):
        expected = read_tree(EXPECTED)
        with tempfile.TemporaryDirectory(prefix="ludus-texture-golden-") as directory:
            actual = cook_all(Path(directory))
        self.assertEqual(sorted(expected), sorted(actual))
        for name, data in expected.items():
            with self.subTest(name=name):
                self.assertEqual(data, actual[name])


if __name__ == "__main__":
    unittest.main()
