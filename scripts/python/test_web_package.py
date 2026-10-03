"""Offline archive contracts; no GPU provider or browser substitution."""
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from web_package import (collect, digest, package, PORT_NOTICES, RUNTIME,
                         SDK_NOTICES, validate_payload, validate_wasm, verify_archive,
                         write_archive)

# One unshared memory, 512 initial / 4096 maximum 64KiB pages.
WASM = b"\0asm\x01\0\0\0\x05\x06\x01\x01\x80\x04\x80\x20"


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.files = {"index.html": b'<script src="index.js"></script>',
                      "iframe.html": b'<iframe src="index.html"></iframe>',
                      "index.js": b'const wasm = "index.wasm";', "index.wasm": WASM}

    def prepare(self):
        build = self.root / "out/build/web-emscripten-release/apps/smoke"
        build.mkdir(parents=True)
        for name in RUNTIME:
            (build / name).write_bytes(self.files[name])
        (self.root / "apps/smoke").mkdir(parents=True)
        (self.root / "apps/smoke/iframe.html").write_bytes(self.files["iframe.html"])
        (self.root / "LICENSE").write_text("test Ludus notice")
        sdk = self.root / "out/host-tools/emsdk/upstream/emscripten"
        for source in SDK_NOTICES:
            path = sdk / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("test runtime notice " + source)
        for source in PORT_NOTICES:
            path = sdk / "cache/ports/emdawnwebgpu/emdawnwebgpu_pkg" / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("test port notice")
        header = sdk / "cache/ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu/include/webgpu/webgpu.h"
        header.parent.mkdir(parents=True)
        header.write_text("// BSD 3-Clause License\n#ifndef WEBGPU_H_\n")
        (self.root / "config").mkdir()
        (self.root / "config/web_toolchain.json").write_text('{"emscripten":"test"}')
        (self.root / "config/shader_toolchain.json").write_text('{"slang":{"version":"test"}}')
        (self.root / "config/spirv_cross_toolchain.json").write_text('{"spirv_cross":{"tag":"test"}}')
        notice = self.root / "out/shader-tools/spirv-cross/LICENSE"
        notice.parent.mkdir(parents=True)
        notice.write_text("test translator notice")

    def test_repeatable_exact_archive_and_manifest(self):
        self.prepare()
        archive = package(self.root)
        first = archive.read_bytes()
        # Stale stage files cannot survive repackaging; source mtimes do not affect ZIP.
        (self.root / "out/packages/ludus-web-smoke/stale.map").write_text("stale")
        package(self.root)
        self.assertEqual(first, archive.read_bytes())
        files = collect(self.root)
        verify_archive(archive, files)
        info = json.loads(files["build-info.json"])
        self.assertEqual(info["sha256"]["index.wasm"], digest(WASM))
        self.assertNotIn("stale.map", files)
        with zipfile.ZipFile(archive) as z:
            self.assertEqual(z.namelist(), sorted(files))
            self.assertTrue(all(i.date_time == (1980, 1, 1, 0, 0, 0) for i in z.infolist()))

    def test_missing_runtime_and_notice_fail(self):
        self.prepare()
        (self.root / "out/build/web-emscripten-release/apps/smoke/index.wasm").unlink()
        with self.assertRaises(OSError):
            collect(self.root)
        (self.root / "out/build/web-emscripten-release/apps/smoke/index.wasm").write_bytes(WASM)
        (self.root / "LICENSE").unlink()
        with self.assertRaises(OSError):
            collect(self.root)

    def test_nonlocal_missing_and_unsafe_assets_rejected(self):
        for ref in ("https://cdn.example/game.js", "/index.js", "Index.js", "missing.js"):
            with self.subTest(ref=ref):
                files = dict(self.files, **{"index.html": f'<script src="{ref}"></script>'.encode()})
                with self.assertRaises(ValueError):
                    validate_payload(files, self.root)
        for path in ("../escape", "/escape", "folder\\escape"):
            with self.assertRaises(ValueError):
                validate_payload(dict(self.files, **{path: b"bad"}), self.root)

    def test_machine_paths_test_provider_and_debug_rejected(self):
        for content in (b'/home/user/game', str(self.root).encode(), b'//# sourceMappingURL=x', b'[W6:passed]'):
            with self.assertRaises(ValueError):
                validate_payload(dict(self.files, **{"index.js": self.files["index.js"] + content}), self.root)
        with self.assertRaises(ValueError):
            validate_wasm(WASM + b'\0\x05\x04name')
        with self.assertRaises(ValueError):
            validate_wasm(WASM[:-1] + b'\x10')  # wrong maximum memory
        with self.assertRaises(ValueError):
            validate_wasm(WASM[:-1])

    def test_archive_tamper_rejected(self):
        path = self.root / "test.zip"
        write_archive(path, dict(self.files, **{"index.js": b"tampered"}))
        with self.assertRaises(ValueError):
            verify_archive(path, self.files)


if __name__ == "__main__":
    unittest.main()
