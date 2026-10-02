"""Unit tests for the ludus_tools host-tooling package.

Run with the pinned-or-newer Python (>=3.10):

    python3 -m unittest test_ludus_tools -v

These tests prove the schema/store/resolution logic that is implementable and
checkable without the native C++ toolchain. Native build/relocation/GUI gates
are tracked in docs/development/project-sdk-workflow-evidence.md.
"""

from __future__ import annotations

import io
import json
import os
import tarfile
import tempfile
import unittest
from pathlib import Path

from ludus_tools import descriptor, identity, lockfile, sdkstore
from ludus_tools.errors import ToolingError

REPO_ROOT = Path(__file__).resolve().parents[2]
FIXTURES = REPO_ROOT / "apps" / "editor" / "tests" / "fixtures"


def _manifest_json(**overrides) -> dict:
    base = {
        "schema_version": 2,
        "name": "Ludus",
        "version": "0.1.0",
        "source_revision": "deadbeef",
        "target_triple": "x86_64-linux-gnu",
        "compiler_id": "Clang",
        "compiler_version": "18.1.8",
        "cxx_runtime_abi": "libstdc++-cxx11",
        "distro_baseline": "ubuntu-24.04",
        "build_flavor": "Development",
        "sdk_variant": "assert-v2-Development-RelWithDebInfo-dialogs-0-asan-0-ubsan-0-tsan-0",
        "cxx_standard": "C++23",
        "library_type": "static",
        "features": ["text", "rhi"],
        "assert_policy": {
            "policy_version": 2,
            "enable_asserts": True,
            "break_on_check": True,
            "dialogs_available": False,
        },
        "package_format": "ludus-sdk-tar-1",
        "components": ["FoundationBase", "GraphicsRhi"],
        "dependencies": [{"name": "volk", "version": "1.4.357.0"}],
        "system_prerequisites": ["vulkan-loader"],
    }
    base.update(overrides)
    return base


def _write_sdk_prefix(root: Path, manifest: dict) -> Path:
    prefix = root
    (prefix / "share" / "Ludus").mkdir(parents=True, exist_ok=True)
    (prefix / "share" / "Ludus" / "LudusSdkManifest.json").write_text(
        json.dumps(manifest, indent=2)
    )
    (prefix / "include" / "ludus").mkdir(parents=True, exist_ok=True)
    (prefix / "include" / "ludus" / "placeholder.h").write_text("#pragma once\n")
    (prefix / "lib").mkdir(parents=True, exist_ok=True)
    (prefix / "lib" / "libludus_foundation_base.a").write_bytes(b"\x00ar")
    return prefix


def _make_sdk_tar(dest_tar: Path, manifest: dict, *, wrap_dir: str = "") -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td) / "prefix"
        _write_sdk_prefix(root, manifest)
        with tarfile.open(dest_tar, "w:gz") as tf:
            base = wrap_dir or "."
            for path in sorted(root.rglob("*")):
                arcname = os.path.join(base, str(path.relative_to(root)))
                tf.add(path, arcname=arcname)


# ---------------------------------------------------------------------------


class DescriptorV1Compat(unittest.TestCase):
    """The v2-aware reader must agree with the shared v1 fixtures.

    The shared ``cases.json`` is the v1 contract fixture consumed by the legacy
    C++ ProjectStore and editor_project.py (whose byte-for-byte agreement with
    these cases is covered by test_editor_tool.py). ludus_tools.descriptor is the
    newer *version-2-aware* reader, so it must still produce the identical
    verdict for every v1 case — with one deliberate exception: a case whose only
    reason to be invalid under the old reader is "version == 2" is now a valid
    v2 descriptor. That one case (``invalid_bad_version.json``) is handled below
    by test_v2_formerly_invalid_version so the preserved-v1 intent stays explicit.
    """

    V2_ACCEPTED_FILES = {"invalid_bad_version.json"}

    def test_shared_fixture_cases(self) -> None:
        cases = json.loads((FIXTURES / "cases.json").read_text())
        for case in cases:
            if case["file"] in self.V2_ACCEPTED_FILES:
                continue
            data = (FIXTURES / case["file"]).read_bytes()
            if case["valid"]:
                d = descriptor.parse_descriptor_bytes(data)
                self.assertEqual(d.version, 1)
                self.assertEqual(d.name, case["name"])
                self.assertEqual(d.provider, case["provider"])
                self.assertEqual(d.run_args, case["run_args"])
            else:
                with self.assertRaises(ToolingError) as ctx:
                    descriptor.parse_descriptor_bytes(data)
                self.assertEqual(ctx.exception.code, case["code"])

    def test_v2_formerly_invalid_version(self) -> None:
        # The v1 fixture marks version==2 invalid (UnsupportedVersion). The
        # v2-aware reader accepts it. But that fixture declares provider "ludus"
        # which must NOT carry an engine object, and omits template — both legal.
        data = (FIXTURES / "invalid_bad_version.json").read_bytes()
        d = descriptor.parse_descriptor_bytes(data)
        self.assertEqual(d.version, 2)
        self.assertEqual(d.provider, "ludus")
        self.assertIsNone(d.engine)

    def test_legacy_reader_still_rejects_v2(self) -> None:
        # editor_project.py (the v1-only reader) must keep rejecting v2 so old
        # Editor builds fail usefully (P09 "Old Editor versions reject v2").
        import editor_project

        data = (FIXTURES / "invalid_bad_version.json").read_bytes()
        with self.assertRaises(editor_project.ProjectError) as ctx:
            editor_project.parse_descriptor_bytes(data)
        self.assertEqual(ctx.exception.code, "UnsupportedVersion")


class DescriptorV2(unittest.TestCase):
    def _v2(self, **over) -> bytes:
        base = {
            "version": 2,
            "name": "MyGame",
            "provider": "cmake",
            "source_dir": ".",
            "preset": "linux-clang-development",
            "target": "my_game",
            "run": {"cwd": ".", "args": []},
            "engine": {"version": "0.1.0", "components": ["FoundationBase"], "features": []},
            "template": {"id": "minimal", "version": 1},
        }
        base.update(over)
        return json.dumps(base).encode("utf-8")

    def test_valid_v2(self) -> None:
        d = descriptor.parse_descriptor_bytes(self._v2())
        self.assertEqual(d.version, 2)
        self.assertIsNotNone(d.engine)
        self.assertEqual(d.engine.version, "0.1.0")
        self.assertEqual(d.engine.components, ["FoundationBase"])
        self.assertEqual(d.template.id, "minimal")
        self.assertEqual(d.template.version, 1)

    def test_release_preset_allowed_v2(self) -> None:
        d = descriptor.parse_descriptor_bytes(self._v2(preset="linux-clang-release"))
        self.assertEqual(d.preset, "linux-clang-release")

    def test_cmake_requires_engine(self) -> None:
        body = json.loads(self._v2())
        del body["engine"]
        with self.assertRaises(ToolingError) as ctx:
            descriptor.parse_descriptor_bytes(json.dumps(body).encode())
        self.assertEqual(ctx.exception.code, "InvalidProject")

    def test_ludus_provider_rejects_engine(self) -> None:
        with self.assertRaises(ToolingError):
            descriptor.parse_descriptor_bytes(self._v2(provider="ludus"))

    def test_floating_version_rejected(self) -> None:
        with self.assertRaises(ToolingError):
            descriptor.parse_descriptor_bytes(self._v2(engine={"version": "^1.0"}))

    def test_unknown_engine_field_rejected(self) -> None:
        with self.assertRaises(ToolingError):
            descriptor.parse_descriptor_bytes(
                self._v2(engine={"version": "1.0", "nope": 1})
            )

    def test_release_preset_rejected_in_v1(self) -> None:
        body = {
            "version": 1,
            "name": "x",
            "provider": "cmake",
            "source_dir": ".",
            "preset": "linux-clang-release",
            "target": "t",
            "run": {"cwd": ".", "args": []},
        }
        with self.assertRaises(ToolingError):
            descriptor.parse_descriptor_bytes(json.dumps(body).encode())

    def test_oversize_rejected(self) -> None:
        with self.assertRaises(ToolingError):
            descriptor.parse_descriptor_bytes(b"{" + b" " * (descriptor.MAX_FILE_BYTES + 1))


class LockSchema(unittest.TestCase):
    def test_unresolved_roundtrip(self) -> None:
        lock = lockfile.unresolved_lock("0.1.0", template_version=1)
        parsed = lockfile.parse_lock_bytes(lock.serialize())
        self.assertFalse(parsed.resolved)
        self.assertEqual(parsed.engine_version, "0.1.0")
        self.assertEqual(parsed.packages, [])

    def test_resolved_requires_packages(self) -> None:
        bad = {
            "schema_version": 1,
            "resolved": True,
            "engine": {"version": "0.1.0", "revision": "abc"},
            "template_version": 1,
            "packages": [],
        }
        with self.assertRaises(ToolingError):
            lockfile.parse_lock_bytes(json.dumps(bad).encode())

    def test_unresolved_forbids_packages(self) -> None:
        bad = {
            "schema_version": 1,
            "resolved": False,
            "engine": {"version": "0.1.0", "revision": ""},
            "template_version": 1,
            "packages": [{"target": "t", "flavor": "Debug", "sdk_variant": "v", "digest": "d", "size": 1}],
        }
        with self.assertRaises(ToolingError):
            lockfile.parse_lock_bytes(json.dumps(bad).encode())

    def test_descriptor_lock_agreement(self) -> None:
        lock = lockfile.unresolved_lock("0.1.0", 1)
        lockfile.validate_descriptor_lock_agreement("0.1.0", lock)
        with self.assertRaises(ToolingError):
            lockfile.validate_descriptor_lock_agreement("9.9.9", lock)

    def test_resolved_roundtrip(self) -> None:
        lock = lockfile.Lock(
            resolved=True,
            engine_version="0.1.0",
            engine_revision="abc",
            template_version=1,
            packages=[
                lockfile.PackageEntry("linux-x64", "Development", "variant", "a" * 64, 1234)
            ],
        )
        parsed = lockfile.parse_lock_bytes(lock.serialize())
        self.assertTrue(parsed.resolved)
        self.assertEqual(len(parsed.packages), 1)
        self.assertEqual(parsed.packages[0].digest, "a" * 64)


class LocalSettings(unittest.TestCase):
    def test_override_set_clear(self) -> None:
        s = lockfile.LocalSettings()
        s.set_override("linux-x64", "Development", "/opt/sdk")
        self.assertEqual(s.find("linux-x64", "Development").prefix, "/opt/sdk")
        parsed = lockfile.parse_local_settings_bytes(s.serialize())
        self.assertEqual(parsed.find("linux-x64", "Development").prefix, "/opt/sdk")
        self.assertTrue(parsed.clear_override("linux-x64", "Development"))
        self.assertIsNone(parsed.find("linux-x64", "Development"))

    def test_missing_file_is_empty(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            s = lockfile.parse_local_settings_file(Path(td) / "local.json")
            self.assertEqual(s.overrides, [])


class Identity(unittest.TestCase):
    def test_compat_flavor_mismatch(self) -> None:
        ident = identity.SdkIdentity.from_json(_manifest_json())
        mism = identity.check_compatible(ident, required_flavor="Release")
        self.assertEqual([m.field_name for m in mism], ["flavor"])

    def test_compat_ok(self) -> None:
        ident = identity.SdkIdentity.from_json(_manifest_json())
        self.assertEqual(identity.check_compatible(ident, required_flavor="Development"), [])

    def test_feature_missing(self) -> None:
        ident = identity.SdkIdentity.from_json(_manifest_json())
        mism = identity.check_compatible(ident, required_features=["audio"])
        self.assertEqual([m.field_name for m in mism], ["features"])

    def test_abi_reference_mismatch(self) -> None:
        a = identity.SdkIdentity.from_json(_manifest_json())
        b = identity.SdkIdentity.from_json(_manifest_json(compiler_version="15.0.0"))
        mism = identity.check_compatible(a, reference=b)
        self.assertIn("compiler_version", [m.field_name for m in mism])

    def test_legacy_flat_manifest(self) -> None:
        legacy = {
            "name": "Ludus",
            "version": "0.1.0",
            "build_flavor": "Development",
            "sdk_variant": "variant",
            "assert_policy_version": 2,
            "enable_asserts": 1,
            "break_on_check": 1,
            "assert_dialogs_available": 0,
        }
        ident = identity.SdkIdentity.from_json(legacy)
        self.assertEqual(ident.assert_policy.policy_version, 2)
        self.assertTrue(ident.assert_policy.enable_asserts)


class Store(unittest.TestCase):
    def setUp(self) -> None:
        self.td = tempfile.TemporaryDirectory()
        self.store = sdkstore.SdkStore(Path(self.td.name) / "store")

    def tearDown(self) -> None:
        self.td.cleanup()

    def test_install_and_reuse(self) -> None:
        tar = Path(self.td.name) / "sdk.tar.gz"
        _make_sdk_tar(tar, _manifest_json())
        installed = self.store.install_archive(tar)
        self.assertTrue(installed.prefix.is_dir())
        self.assertTrue((installed.prefix / "share" / "Ludus" / "LudusSdkManifest.json").is_file())
        # Second install of the identical archive reuses the same directory.
        again = self.store.install_archive(tar)
        self.assertEqual(installed.prefix, again.prefix)
        self.assertEqual(len(self.store.list_installed()), 1)

    def test_install_wrapped_dir(self) -> None:
        tar = Path(self.td.name) / "wrapped.tar.gz"
        _make_sdk_tar(tar, _manifest_json(), wrap_dir="ludus-sdk")
        installed = self.store.install_archive(tar)
        self.assertTrue((installed.prefix / "share" / "Ludus" / "LudusSdkManifest.json").is_file())

    def test_digest_mismatch_rejected(self) -> None:
        tar = Path(self.td.name) / "sdk.tar.gz"
        _make_sdk_tar(tar, _manifest_json())
        with self.assertRaises(ToolingError) as ctx:
            self.store.install_archive(tar, expected_digest="0" * 64)
        self.assertEqual(ctx.exception.code, "DigestMismatch")

    def test_resolve_missing(self) -> None:
        with self.assertRaises(ToolingError) as ctx:
            self.store.resolve_installed("f" * 64)
        self.assertEqual(ctx.exception.code, "SdkNotFound")

    def test_traversal_archive_rejected(self) -> None:
        tar = Path(self.td.name) / "evil.tar"
        with tarfile.open(tar, "w") as tf:
            info = tarfile.TarInfo("../escape.txt")
            data = b"nope"
            info.size = len(data)
            tf.addfile(info, io.BytesIO(data))
        with self.assertRaises(ToolingError) as ctx:
            self.store.validate_archive(tar)
        self.assertEqual(ctx.exception.code, "ArchiveInvalid")

    def test_absolute_name_rejected(self) -> None:
        tar = Path(self.td.name) / "abs.tar"
        with tarfile.open(tar, "w") as tf:
            info = tarfile.TarInfo("/etc/passwd")
            info.size = 0
            tf.addfile(info, io.BytesIO(b""))
        with self.assertRaises(ToolingError) as ctx:
            self.store.validate_archive(tar)
        self.assertEqual(ctx.exception.code, "ArchiveInvalid")

    def test_absolute_symlink_rejected(self) -> None:
        tar = Path(self.td.name) / "link.tar"
        with tarfile.open(tar, "w") as tf:
            info = tarfile.TarInfo("link")
            info.type = tarfile.SYMTYPE
            info.linkname = "/etc/passwd"
            tf.addfile(info)
        with self.assertRaises(ToolingError) as ctx:
            self.store.validate_archive(tar)
        self.assertEqual(ctx.exception.code, "ArchiveInvalid")

    def test_cancellation(self) -> None:
        tar = Path(self.td.name) / "sdk.tar.gz"
        _make_sdk_tar(tar, _manifest_json())
        token = sdkstore.CancelToken()
        token.cancel()
        with self.assertRaises(ToolingError) as ctx:
            self.store.install_archive(tar, cancel=token)
        self.assertEqual(ctx.exception.code, "InstallCancelled")


if __name__ == "__main__":
    unittest.main()
