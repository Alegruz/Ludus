"""Document ownership, closed metadata and all-or-none edits."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import play_documents as documents

GAME = "0000000000000001"
OBJECT = "000000000000a001"
PROPERTY = "0000000000000001"


class PlayDocumentsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.obj = {"version": 1, "game": GAME, "objects": [
            {"id": OBJECT, "properties": [{"id": PROPERTY, "kind": "float32", "value": 1.0}]}]}
        self.path = self.root / "game.tuning.json"
        self.path.write_text(json.dumps(self.obj))

    def sidecar(self, **changes):
        obj = {"version": 1, "host_target": "game_host", "module_target": "game_module",
               "startup_document": "game.tuning.json", "watch_roots": ["src"]}
        obj.update(changes)
        (self.root / "ludus.play.json").write_text(json.dumps(obj))

    def edit(self, value=2.0):
        return {"object": OBJECT, "property": PROPERTY, "kind": "float32", "value": value}

    def test_open_is_optional_and_does_not_execute_project_code(self):
        with patch("subprocess.Popen", side_effect=AssertionError("open executed code")):
            self.assertIsNone(documents.read_play_descriptor(self.root))
            self.sidecar()
            descriptor = documents.read_play_descriptor(self.root)
            self.assertEqual(descriptor.startup_document, self.path)
            self.assertEqual(descriptor.watch_roots, (self.root / "src",))
            self.assertFalse(documents.TuningDocument(self.path).dirty)

    def test_closed_sidecar_version_targets_and_bounds(self):
        for changes in ({"version": True}, {"extra": 3}, {"host_target": "a;evil"},
                        {"module_target": "game_host"}, {"watch_roots": [str(i) for i in range(17)]},
                        {"watch_roots": ["src", "./src"]}, {"startup_document": "../escape"},
                        {"startup_document": str(self.path)}):
            with self.subTest(changes=changes):
                self.sidecar(**changes)
                with self.assertRaises(documents.DocumentError):
                    documents.read_play_descriptor(self.root)

    def test_symlink_escape_and_oversized_files_reject(self):
        (self.root / "outside").symlink_to(self.root.parent, target_is_directory=True)
        self.sidecar(startup_document="outside/file")
        with self.assertRaises(documents.DocumentError):
            documents.read_play_descriptor(self.root)
        self.path.write_bytes(b" " * (documents.MAX_DOCUMENT_BYTES + 1))
        with self.assertRaises(documents.DocumentError):
            documents.TuningDocument(self.path)

    def test_invalid_values_duplicate_ids_and_fields(self):
        cases = [("bool", 1), ("int32", True), ("int32", 2**31), ("enum", -1),
                 ("float32", float("nan")), ("float32", float("inf")), ("float32", 10**1000),
                 ("string", "\0"), ("string", "é" * 129), ("string", "\ud800")]
        for kind, value in cases:
            with self.subTest(kind=kind, value=repr(value)[:30]):
                with self.assertRaises(documents.DocumentError):
                    documents.validate_value(kind, value)
        for data in (b'{"version":1,"version":1}', b'{"value":NaN}', b'{"text":"\xff"}'):
            with self.assertRaises(documents.DocumentError):
                documents.decode(data)
        candidate = copy.deepcopy(self.obj)
        candidate["objects"].append(copy.deepcopy(candidate["objects"][0]))
        with self.assertRaises(documents.DocumentError):
            documents.validate_tuning(candidate)
        candidate = copy.deepcopy(self.obj)
        candidate["objects"][0]["properties"] *= 2
        with self.assertRaises(documents.DocumentError):
            documents.validate_tuning(candidate)

    def test_apply_is_dirty_only_and_undo_is_independent_of_disk(self):
        doc = documents.TuningDocument(self.path)
        self.assertFalse(doc.can_undo)
        self.assertFalse(doc.can_redo)
        original = self.path.read_bytes()
        revision = documents.digest(doc.encoded())
        doc.apply([self.edit()], expected_digest=revision)
        self.assertTrue(doc.dirty)
        self.assertTrue(doc.can_undo)
        self.assertFalse(doc.can_redo)
        self.assertEqual(self.path.read_bytes(), original)
        self.assertTrue(doc.undo())
        self.assertFalse(doc.dirty)
        self.assertFalse(doc.can_undo)
        self.assertTrue(doc.can_redo)
        self.assertTrue(doc.redo())
        self.assertTrue(doc.dirty)
        doc.discard()
        self.assertFalse(doc.dirty)
        self.assertFalse(doc.can_undo)
        self.assertFalse(doc.can_redo)
        doc.apply([self.edit()], expected_digest=documents.digest(doc.encoded()))
        doc.save()
        self.assertFalse(doc.dirty)
        self.assertEqual(doc.saved_digest, documents.digest(self.path.read_bytes()))
        self.assertTrue(doc.undo())
        self.assertTrue(doc.dirty)
        self.assertEqual(json.loads(self.path.read_bytes())["objects"][0]["properties"][0]["value"], 2)

    def test_stale_revision_rejected_batch_and_external_save_conflict(self):
        doc = documents.TuningDocument(self.path)
        before = doc.encoded()
        revision = documents.digest(before)
        bad = self.edit(3.0)
        bad["property"] = "0000000000000002"
        with self.assertRaises(documents.DocumentError):
            doc.apply([self.edit(), bad], expected_digest=revision)
        self.assertEqual(doc.encoded(), before)
        with self.assertRaises(documents.DocumentError):
            doc.apply([self.edit(), self.edit()], expected_digest=revision)
        doc.apply([self.edit()], expected_digest=revision)
        with self.assertRaises(documents.DocumentError):
            doc.apply([self.edit(3.0)], expected_digest=revision)
        external = b'{"external":true}'
        self.path.write_bytes(external)
        with self.assertRaises(documents.DocumentError):
            doc.save()
        self.assertEqual(self.path.read_bytes(), external)
        self.assertTrue(doc.dirty)

    def test_failed_atomic_replace_preserves_saved_document_and_dirty_draft(self):
        doc = documents.TuningDocument(self.path)
        before = self.path.read_bytes()
        doc.apply([self.edit()], expected_digest=documents.digest(doc.encoded()))
        with patch("play_documents.os.replace", side_effect=OSError("disk error")):
            with self.assertRaises(OSError):
                doc.save()
        self.assertEqual(self.path.read_bytes(), before)
        self.assertTrue(doc.dirty)
        self.assertFalse(list(self.root.glob(".tuning-*")))


if __name__ == "__main__":
    unittest.main()
