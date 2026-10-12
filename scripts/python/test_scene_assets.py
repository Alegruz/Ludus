"""Independent storage acceptance fixtures; no PBRT input is needed."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
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


    def test_cli_import_rolls_back_catalog_and_level_on_save_failure(self):
        value = self.imported()
        relative = "scenes/test.lulevel"
        path = self.root / relative
        previous = assets.save_level(self.root, relative, value)
        old_catalog = assets.read(self.root / "assets/catalog.json")
        (self.source / "source").write_text("changed source")
        real_replace = assets.os.replace
        def replace(source, destination):
            if Path(destination).resolve() == path.resolve() and Path(source).name == "next-level":
                raise OSError("Simulated failed level replacement")
            return real_replace(source, destination)
        with patch.object(assets.scene_cook, "cook", side_effect=fixture), patch.object(assets.os, "replace", side_effect=replace):
            with self.assertRaises(ToolingError) as error:
                assets.import_saved_level(self.root, relative, self.source)
        self.assertEqual("StorageFailure", error.exception.code)
        self.assertEqual(previous, assets.read(path))
        self.assertEqual(old_catalog, assets.read(self.root / "assets/catalog.json"))
        self.assertEqual(value, assets.open_level(self.root, relative))
        self.assertFalse(list(self.root.rglob(".scene-import-*")))

    def test_cli_import_rolls_back_both_files_after_sync_failure(self):
        value = self.imported()
        relative = "scenes/test.lulevel"
        previous = assets.save_level(self.root, relative, value)
        old_catalog = assets.read(self.root / "assets/catalog.json")
        (self.source / "source").write_text("changed source")
        calls = 0
        def sync(_path):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise OSError("Simulated publication sync failure")
        with patch.object(assets.scene_cook, "cook", side_effect=fixture), patch.object(assets, "_sync_directory", side_effect=sync):
            with self.assertRaises(ToolingError):
                assets.import_saved_level(self.root, relative, self.source)
        self.assertEqual(previous, assets.read(self.root / relative))
        self.assertEqual(old_catalog, assets.read(self.root / "assets/catalog.json"))

    def test_cli_import_preserves_external_level_edit_before_commit(self):
        value = self.imported()
        relative = "scenes/test.lulevel"
        path = self.root / relative
        assets.save_level(self.root, relative, value)
        old_catalog = assets.read(self.root / "assets/catalog.json")
        edited = dict(value, name="External edit")
        def changing(source, output):
            result = fixture(source, output)
            path.write_bytes(assets.canonical(edited))
            return result
        with patch.object(assets.scene_cook, "cook", side_effect=changing):
            with self.assertRaises(ToolingError) as error:
                assets.import_saved_level(self.root, relative, self.source)
        self.assertEqual("Conflict", error.exception.code)
        self.assertEqual(assets.canonical(edited), assets.read(path))
        self.assertEqual(old_catalog, assets.read(self.root / "assets/catalog.json"))

    def test_other_process_cannot_read_midway_through_pair_publication(self):
        value = self.imported()
        assets.save_level(self.root, "scenes/test.lulevel", value)
        with assets.writer(self.root):
            result = subprocess.run([sys.executable, "-c",
                "from pathlib import Path; import sys; from ludus_tools import scene_assets as a; "
                "a.open_level(Path(sys.argv[1]),'scenes/test.lulevel')", str(self.root)],
                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Another scene publication is active", result.stderr)
        self.assertEqual(value, assets.open_level(self.root, "scenes/test.lulevel"))


    def test_unwritable_level_stage_fails_before_catalog_publication(self):
        value = self.imported()
        relative = "scenes/test.lulevel"
        previous = assets.save_level(self.root, relative, value)
        old_catalog = assets.read(self.root / "assets/catalog.json")
        real_stage = assets.tempfile.mkdtemp
        def stage(*args, **kwargs):
            if Path(kwargs.get("dir", "/")).resolve() == (self.root / "scenes").resolve():
                raise PermissionError("Unwritable scenes directory")
            return real_stage(*args, **kwargs)
        with patch.object(assets.scene_cook, "cook", side_effect=fixture), patch.object(assets.tempfile, "mkdtemp", side_effect=stage):
            with self.assertRaises(ToolingError):
                assets.import_saved_level(self.root, relative, self.source)
        self.assertEqual(previous, assets.read(self.root / relative))
        self.assertEqual(old_catalog, assets.read(self.root / "assets/catalog.json"))

    def test_editor_open_never_reports_an_external_revision_as_its_loaded_bytes(self):
        from ludus_tools import scene_editor
        value = self.imported()
        relative = "scenes/test.lulevel"
        assets.save_level(self.root, relative, value)
        real_assemble = assets.assemble
        def assemble(root, level, current=None):
            data = real_assemble(root, level, current)
            (self.root / relative).write_bytes(assets.canonical(dict(value, name="External")))
            return data
        request = dict(schema=1, operation="open", project=str(self.root), path=relative,
                       source="", level={}, expected=None)
        with patch.object(assets, "assemble", side_effect=assemble):
            with self.assertRaises(ToolingError) as error:
                scene_editor.operate(request, self.base / "scratch.luscn")
        self.assertEqual("Conflict", error.exception.code)


    def test_cli_pair_import_succeeds_from_an_empty_saved_level(self):
        relative = "scenes/test.lulevel"
        assets.save_level(self.root, relative, assets.new_level())
        with patch.object(assets.scene_cook, "cook", side_effect=fixture):
            result = assets.import_saved_level(self.root, relative, self.source)
        self.assertEqual(1, len(result["objects"]))
        self.assertEqual(result, assets.open_level(self.root, relative))
        self.assertFalse(list(self.root.rglob(".scene-import-*")))

    def test_first_cli_import_failure_removes_its_uncommitted_catalog(self):
        relative = "scenes/test.lulevel"
        path = self.root / relative
        previous = assets.save_level(self.root, relative, assets.new_level())
        real_replace = assets.os.replace
        def replace(source, destination):
            if Path(destination).resolve() == path.resolve() and Path(source).name == "next-level":
                raise OSError("Simulated first import save failure")
            return real_replace(source, destination)
        with patch.object(assets.scene_cook, "cook", side_effect=fixture), patch.object(assets.os, "replace", side_effect=replace):
            with self.assertRaises(ToolingError):
                assets.import_saved_level(self.root, relative, self.source)
        self.assertEqual(previous, assets.read(path))
        self.assertFalse((self.root / "assets/catalog.json").exists())


if __name__ == "__main__":
    unittest.main()
