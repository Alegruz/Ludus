"""Independent std140 checks for the reference renderer's fixed vector arrays."""
import importlib.util
from pathlib import Path
import unittest
root = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("shader", root / "cmake/shaders/compile_shader.py")
shader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shader)
class LayoutTests(unittest.TestCase):
    def layout(self, fields):
        return shader.glsl_es_layout("layout(std140) uniform World {" + fields + "};")
    def test_game_upload(self):
        result = self.layout("vec4 view; vec4 settings; vec4 geometry[64]; vec4 color[64]; vec4 rotation[64];")
        self.assertEqual(result['offsets'], dict(view=0, settings=16, geometry=32, color=1056, rotation=2080))
        self.assertEqual(result['size'], 3104)
    def test_compiler_array_wrapper(self):
        result = shader.glsl_es_layout("struct A { highp vec4 data[64]; }; layout(std140) uniform World { vec4 view; vec4 settings; A geometry; A color; A rotation; } world;")
        self.assertEqual(result['offsets'], dict(view=0, settings=16, geometry=32, color=1056, rotation=2080))
        self.assertEqual(result['size'], 3104)
        for body in ("vec4 data[64]; float extra;", "mat4 data[64];", "A data[2];"):
            with self.subTest(body=body), self.assertRaises(RuntimeError):
                shader.glsl_es_layout("struct A {" + body + "}; layout(std140) uniform World { A geometry; } world;")
    def test_array_stride_and_following_scalar(self):
        result = self.layout("float a[2]; vec3 b[2]; float c;")
        self.assertEqual(result['offsets'], dict(a=0, b=32, c=64))
        self.assertEqual(result['size'], 80)
    def test_unsupported_or_overlarge_layouts_fail(self):
        for fields in ("vec4 a[0];", "vec4 a[257];", "vec4 a[256]; float b;", "mat4 a;", "dvec4 a;", "vec4 a[N];", "vec4 a; vec4 a;"):
            with self.subTest(fields=fields), self.assertRaises(RuntimeError):
                self.layout(fields)
if __name__ == '__main__': unittest.main()
