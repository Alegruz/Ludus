"""Independent storage acceptance fixtures; no PBRT input is needed."""
import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch

from ludus_tools import scene_assets as assets
from ludus_tools.errors import ToolingError


def fixture(source, output):
    output = Path(output)
    data = bytearray(b"LUSCN01\0" + struct.pack("<5I10f", 1, 1, 1, 0, 1, 0, 0, 3, 0, 0, 0, 0, 1, 0, 1))
    data += struct.pack("<I5f2I", 0xffffffff, 0, .5, 1, 1, 1, 3, 3)
    for x, y in ((0, 0), (1, 0), (0, 1)):
        data += struct.pack("<8f", x, y, 0, 0, 0, 1, 0, 0)
    data += struct.pack("<3I2I12f", 0, 1, 2, 0, 0, *assets.IDENTITY)
    (output / "scene.luscn").write_bytes(data)
    (output / "LICENSE.txt").write_text("Synthetic fixture license\n")
    return {"version": 1, "inputs": {"source": assets.digest((source / "source").read_bytes())}}


class SceneAssetsTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.root = self.base / "project"
        self.source = self.base / "external"
        self.root.mkdir()
        self.source.mkdir()
        (self.source / "source").write_text("source")

    def imported(self, level=None, cancelled=lambda: False):
        with patch.object(assets.scene_cook, "cook", side_effect=fixture):
            return assets.import_scene(self.root, self.source, level or assets.new_level(), cancelled)

    def test_move_and_reopen_without_source_or_importer(self):
        value = self.imported()
        binary = assets.assemble(self.root, value)
        assets.save_level(self.root, "scenes/test.lulevel", value)
        shutil.rmtree(self.source)
        moved = self.base / "moved"
        self.root.rename(moved)
        with patch.object(assets.scene_cook, "cook", side_effect=AssertionError("import on reopen")):
            self.assertEqual(value, assets.open_level(moved, "scenes/test.lulevel"))
            self.assertEqual(binary, assets.assemble(moved, value))

    def test_duplicate_and_two_levels_share_assets(self):
        one = self.imported()
        previous = assets.read(self.root / "assets/catalog.json")
        two = self.imported(one)
        other = self.imported()
        self.assertEqual(previous, assets.read(self.root / "assets/catalog.json"))
        self.assertEqual(one["objects"][0], two["objects"][0])
        self.assertNotEqual(two["objects"][0]["id"], two["objects"][1]["id"])
        self.assertEqual(one["objects"][0]["mesh"], other["objects"][0]["mesh"])
        assets.save_level(self.root, "scenes/one.lulevel", two)
        assets.save_level(self.root, "scenes/two.lulevel", other)

    def test_cancelled_or_failed_import_retains_catalog_and_level(self):
        value = self.imported()
        before = assets.read(self.root / "assets/catalog.json")
        with patch.object(assets.scene_cook, "cook", side_effect=ValueError("unsupported")):
            with self.assertRaises(ToolingError):
                assets.import_scene(self.root, self.source, value)
        count = 0
        def cancel():
            nonlocal count
            count += 1
            return count > 1
        with self.assertRaises(ToolingError):
            self.imported(value, cancel)
        self.assertEqual(before, assets.read(self.root / "assets/catalog.json"))
        self.assertEqual(1, len(value["objects"]))

    def test_corrupt_dependency_rejects_open_and_save(self):
        value = self.imported()
        before = assets.save_level(self.root, "scenes/test.lulevel", value)
        record = next(iter(assets.catalog(self.root)["resources"].values()))
        assets.inside(self.root, record["path"]).write_bytes(b"corrupt")
        for operation in (lambda: assets.open_level(self.root, "scenes/test.lulevel"),
                          lambda: assets.save_level(self.root, "scenes/test.lulevel", value, before)):
            with self.assertRaises(ToolingError):
                operation()
        self.assertEqual(before, assets.read(self.root / "scenes/test.lulevel"))

    def test_save_conflict_and_source_edit_conflict(self):
        value = self.imported()
        before = assets.save_level(self.root, "scenes/test.lulevel", value)
        with self.assertRaises(ToolingError) as error:
            assets.save_level(self.root, "scenes/test.lulevel", value, b"stale")
        self.assertEqual("Conflict", error.exception.code)
        def changing(source, output):
            result = fixture(source, output)
            (source / "source").write_text("changed")
            return result
        catalog = assets.read(self.root / "assets/catalog.json")
        with patch.object(assets.scene_cook, "cook", side_effect=changing):
            with self.assertRaises(ToolingError):
                assets.import_scene(self.root, self.source, value)
        self.assertEqual(catalog, assets.read(self.root / "assets/catalog.json"))
        self.assertEqual(before, assets.read(self.root / "scenes/test.lulevel"))

    def test_deterministic_release_contains_licenses(self):
        value = self.imported()
        one, two = self.base / "release1", self.base / "release2"
        assets.publish(self.root, value, one)
        assets.publish(self.root, value, two)
        for path in one.rglob("*"):
            if path.is_file():
                self.assertEqual(path.read_bytes(), (two / path.relative_to(one)).read_bytes())
        self.assertTrue(any(r["kind"] == "license" for r in assets.closure(one, value).values()))

    def test_path_escape_cycle_and_bad_binary_rejected(self):
        with self.assertRaises(ToolingError):
            assets.inside(self.root, "../escape")
        value = self.imported()
        value["objects"][0]["parent"] = value["objects"][0]["id"]
        with self.assertRaises(ToolingError):
            assets.validate_level(value)
        with self.assertRaises(ToolingError):
            assets.split_bundle(b"LUSCN01\0")

    def test_over_capacity_retains_last_good(self):
        value = self.imported()
        for _ in range(127):
            item = dict(value["objects"][0], id=str(assets.uuid.uuid4()))
            value["objects"].append(item)
        before = assets.read(self.root / "assets/catalog.json")
        with self.assertRaises(ToolingError):
            self.imported(value)
        self.assertEqual(before, assets.read(self.root / "assets/catalog.json"))

    def test_prepared_revision_keeps_published_ids_and_catalog_until_commit(self):
        first = self.imported()
        before = assets.read(self.root / "assets/catalog.json")
        old = assets.catalog(self.root)
        (self.source / "source").write_text("new revision")
        with patch.object(assets.scene_cook, "cook", side_effect=fixture):
            candidate, current, expected = assets.prepare_import(self.root, self.source, first)
        self.assertEqual(before, assets.read(self.root / "assets/catalog.json"))
        mesh = first["objects"][0]["mesh"]
        self.assertEqual(mesh, candidate["objects"][1]["mesh"])
        self.assertNotEqual(old["resources"][mesh]["key"], current["resources"][mesh]["key"])
        with self.assertRaises(ToolingError):
            assets.commit_import(self.root, candidate, current, expected, self.source, lambda: True)
        self.assertEqual(before, assets.read(self.root / "assets/catalog.json"))
        assets.commit_import(self.root, candidate, current, expected, self.source)
        self.assertEqual(current, assets.catalog(self.root))
        # Asset identity stays stable while its explicitly committed artifact changes.
        self.assertEqual(mesh, first["objects"][0]["mesh"])
        self.assertTrue(assets.assemble(self.root, first))

    def test_stale_prepared_catalog_cannot_replace_newer_publication(self):
        first = self.imported()
        with patch.object(assets.scene_cook, "cook", side_effect=fixture):
            candidate, current, expected = assets.prepare_import(self.root, self.source, first)
        assets.atomic(self.root / "assets/catalog.json", assets.read(self.root / "assets/catalog.json") + b" ")
        before = assets.read(self.root / "assets/catalog.json")
        with self.assertRaises(ToolingError) as error:
            assets.commit_import(self.root, candidate, current, expected, self.source)
        self.assertEqual("Conflict", error.exception.code)
        self.assertEqual(before, assets.read(self.root / "assets/catalog.json"))


if __name__ == "__main__":
    unittest.main()
