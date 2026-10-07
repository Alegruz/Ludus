"""Regressions for editor payload admission and pinned source archives."""
import hashlib
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

from editor_web import PAYLOAD, download, extract, validate_package


class EditorPackageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.site = Path(self.directory.name)
        for name in PAYLOAD:
            (self.site / name).write_text("ludus_editor.wasm")
        (self.site / "ludus_editor.wasm").write_bytes(b"\0asm\x01\0\0\0\x05\x06\x01\x01\x80\x04\x80\x20")
        (self.site / "licenses").mkdir()
        for name in ("Ludus.txt", "Qt-LGPL-3.0.txt", "Emscripten.txt", "miniaudio.txt", "yyjson.txt"):
            (self.site / "licenses" / name).write_text("notice")
        self.manifest()

    def manifest(self, **values):
        data = {"single_threaded": True, "sha256": {
            name: hashlib.sha256((self.site / name).read_bytes()).hexdigest() for name in PAYLOAD}}
        data.update(values)
        (self.site / "build-info.json").write_text(json.dumps(data))

    def test_complete_package(self):
        validate_package(self.site)

    def test_modified_missing_and_symlinked_assets(self):
        asset = self.site / "editor.js"
        asset.write_text("changed")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            validate_package(self.site)
        asset.unlink()
        with self.assertRaisesRegex(ValueError, "Missing or symlinked"):
            validate_package(self.site)
        asset.symlink_to(self.site / "index.html")
        with self.assertRaisesRegex(ValueError, "Missing or symlinked"):
            validate_package(self.site)

    def test_shared_memory_and_false_identity(self):
        wasm = self.site / "ludus_editor.wasm"
        wasm.write_bytes(wasm.read_bytes().replace(b"\x05\x06\x01\x01", b"\x05\x06\x01\x03"))
        self.manifest()
        with self.assertRaisesRegex(ValueError, "memory policy"):
            validate_package(self.site)
        wasm.write_bytes(wasm.read_bytes().replace(b"\x05\x06\x01\x03", b"\x05\x06\x01\x01"))
        self.manifest(single_threaded=False)
        with self.assertRaisesRegex(ValueError, "shared memory"):
            validate_package(self.site)

    def test_license_cannot_be_omitted(self):
        (self.site / "licenses/Qt-LGPL-3.0.txt").unlink()
        with self.assertRaisesRegex(ValueError, "Missing editor license"):
            validate_package(self.site)

    def test_verified_archive_is_reused_without_network(self):
        archive = self.site / "source.tar"
        archive.write_bytes(b"source")
        with patch("editor_web.urllib.request.urlretrieve") as retrieve:
            download("https://example.test/source", archive, hashlib.sha256(b"source").hexdigest())
            retrieve.assert_not_called()

    def test_unverified_archive_is_rejected(self):
        archive = self.site / "source.tar"
        archive.write_bytes(b"source")
        with patch("editor_web.urllib.request.urlretrieve"):
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                download("https://example.test/source", archive, "wrong")

    def test_archive_cannot_escape_output(self):
        archive = self.site / "source.tar"
        with tarfile.open(archive, "w") as target:
            entry = tarfile.TarInfo("../escaped")
            entry.size = 1
            target.addfile(entry, io.BytesIO(b"x"))
        with self.assertRaises(tarfile.FilterError):
            extract(archive, self.site / "extract")
        self.assertFalse((self.site / "escaped").exists())


if __name__ == "__main__":
    unittest.main()
