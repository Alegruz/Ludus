"""Independent geometry/encoding checks for the scene-specific offline cooker."""

import math
from pathlib import Path
import struct
import tempfile
import unittest

import cook


class CookerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def ply(self, face=(0, 1, 2), trailing=b""):
        header = b"ply\nformat binary_little_endian 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n"
        data = (
            header
            + struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
            + struct.pack("<B3i", 3, *face)
            + trailing
        )
        path = self.root / "mesh.ply"
        path.write_bytes(data)
        return path

    def test_ply_geometry_and_indices(self):
        points, triangles = cook.read_ply(self.ply())
        self.assertEqual(triangles, [(0, 1, 2)])
        self.assertEqual(points[1][0], (1, 0, 0))
        self.assertEqual(points[0][2], (0, 1))

    def test_ply_rejects_index_and_trailing_data(self):
        for path in (self.ply((0, 1, 3)),):
            with self.assertRaises(ValueError):
                cook.read_ply(path)
        with self.assertRaises(ValueError):
            cook.read_ply(self.ply(trailing=b"x"))

    def test_ply_truncation(self):
        path = self.ply()
        path.write_bytes(path.read_bytes()[:-2])
        with self.assertRaises((ValueError, struct.error)):
            cook.read_ply(path)

    def test_tga_bottom_origin_is_top_left_rgba(self):
        header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 1, 2, 24, 0)
        path = self.root / "texture.tga"
        path.write_bytes(header + b"\x00\x00\xff\xff\x00\x00")
        self.assertEqual(
            cook.read_tga(path), (1, 2, bytearray([0, 0, 255, 255, 255, 0, 0, 255]))
        )

    def test_tga_rejects_truncation_and_transparency(self):
        path = self.root / "texture.tga"
        header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 1, 1, 32, 0x28)
        for data in (header + b"\x00", header + b"\x00\x00\xff\x7f"):
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                cook.read_tga(path)

    def test_scoped_material_and_camera_from_world(self):
        path = self.root / "scene.pbrt"
        path.write_text(
            'Transform [1 0 0 0 0 1 0 0 0 0 1 0 -2 0 0 1]\nCamera "perspective" "float fov" [70]\nWorldBegin\nMaterial "diffuse" "rgb reflectance" [1 0 0]\nAttributeBegin\nMaterial "diffuse" "rgb reflectance" [0 1 0]\nShape "disk" "float radius" [1]\nAttributeEnd\nShape "disk" "float radius" [1]\nWorldEnd\n'
        )
        camera, fov, _, _, shapes = cook.parse_scene(path)
        self.assertEqual(camera[12], -2)
        self.assertEqual(fov, 70)
        self.assertEqual(shapes[0]["material"]["rgb reflectance"], ["0", "1", "0"])
        self.assertEqual(shapes[1]["material"]["rgb reflectance"], ["1", "0", "0"])
        self.assertEqual(shapes[0]["transform"], cook.IDENTITY)

    def test_atlas_clipping_preserves_area_and_repetition(self):
        vertices = [
            ((0, 0, 0), (0, 0, 1), (-0.5, -0.5)),
            ((2, 0, 0), (0, 0, 1), (1.5, -0.5)),
            ((0, 2, 0), (0, 0, 1), (-0.5, 1.5)),
        ]
        triangles = cook.atlas_triangles(vertices, 544, 516)
        area = sum(
            math.sqrt(cook.dot(n, n)) / 2
            for a, b, c in triangles
            for n in [cook.cross(cook.sub(b[0], a[0]), cook.sub(c[0], a[0]))]
        )
        self.assertAlmostEqual(area, 2)
        for triangle in triangles:
            for _, _, (u, v) in triangle:
                self.assertGreaterEqual(u, 2 / 544)
                self.assertLessEqual(u, 514 / 544)
                self.assertGreaterEqual(v, 2 / 516)
                self.assertLessEqual(v, 514 / 516)

    def test_rectangle_preserves_area_and_facing(self):
        points = [
            ((x, y, 2), None, (0, 0)) for x, y in ((0, 0), (0, 2), (4, 2), (4, 0))
        ]
        result = cook.rectangle(points, [(0, 1, 2), (0, 2, 3)])
        self.assertEqual(result[9:], [2, 1, 2])
        self.assertLess(cook.cross(result[:3], result[3:6])[2], 0)
        self.assertEqual(
            4
            * math.sqrt(
                cook.dot(result[:3], result[:3]) * cook.dot(result[3:6], result[3:6])
            ),
            8,
        )

    def test_uv_work_is_bounded(self):
        vertices = [
            ((0, 0, 0), (0, 0, 1), (0, 0)),
            ((1, 0, 0), (0, 0, 1), (1e8, 0)),
            ((0, 1, 0), (0, 0, 1), (0, 1)),
        ]
        with self.assertRaises(ValueError):
            cook.atlas_triangles(vertices, 544, 516)

    def test_asset_path_cannot_escape_source(self):
        with self.assertRaises(ValueError):
            cook.asset_path(self.root, "../outside")
        with self.assertRaises(ValueError):
            cook.asset_path(self.root, "/outside")


if __name__ == "__main__":
    unittest.main()
