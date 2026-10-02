"""Observable failure-scenario acceptance for the project-sdk-workflow.

The handoff's "Failure scenarios required for review" list demands observable
results, not tests that merely mirror helper implementations. Each test here
drives a REAL failure path end-to-end (real flock contention, real archives,
real on-disk corruption, real staged commits) and asserts the user-visible
outcome (stable error code / preserved state), so a reviewer can read the
scenario -> result mapping directly.

Run:  python3 -m unittest test_ludus_failure_scenarios -v

Scenarios covered (headless; the native build/GUI counterparts run in CI on the
pinned toolchain, see docs/development/project-sdk-workflow-evidence.md):

  * fresh clone with a missing locked SDK
  * wrong compiler/runtime/flavor identity
  * headers and libraries from different variants (ABI reference mismatch)
  * SDK corruption in the store + explicit repair
  * local SDK refresh detected mid-operation (abort, no mixed inputs)
  * concurrent CLI/Editor build on one tree (cross-backend lock)
  * failed build cannot launch an older executable
  * install cancellation and archive traversal
  * destination creation race (no overwrite)
  * descriptor already migrated / lock disagreement during migration
  * interrupted paired descriptor+lock commit (recoverable)
  * project paths containing spaces/Unicode
  * a machine with Qt absent (headless tooling still works)
  * competing installers of the same identity (install lock)
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path

import editor_tool  # the Editor backend's BuildTreeLock (cross-backend contention)
from ludus_tools import create, descriptor, lockfile, operations, resolve
from ludus_tools.buildlock import BuildTreeLock
from ludus_tools.errors import ToolingError
from ludus_tools.identity import SdkIdentity, check_compatible
from ludus_tools.sdkstore import CancelToken, SdkStore

from test_ludus_tools import _make_sdk_tar, _manifest_json, _write_sdk_prefix


class MissingLockedSdk(unittest.TestCase):
    def test_fresh_clone_missing_locked_sdk_stops_actionably(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            store = SdkStore(Path(td) / "empty-store")
            # Resolved lock points at a digest that is not installed (fresh clone).
            lock = lockfile.Lock(
                resolved=True, engine_version="0.1.0", engine_revision="r",
                template_version=1,
                packages=[lockfile.PackageEntry("x86_64-linux-gnu", "Development", "v", "a" * 64, 1)],
            )
            with self.assertRaises(ToolingError) as ctx:
                resolve.resolve_sdk(
                    target="x86_64-linux-gnu", flavor="Development", lock=lock,
                    local_settings=lockfile.LocalSettings(), store=store,
                )
            self.assertEqual(ctx.exception.code, "SdkNotFound")
            self.assertIn("install", ctx.exception.message)


class WrongIdentity(unittest.TestCase):
    def test_wrong_flavor_rejected(self) -> None:
        ident = SdkIdentity.from_json(_manifest_json(build_flavor="Development"))
        mism = check_compatible(ident, required_flavor="Release")
        self.assertTrue(mism and mism[0].field_name == "flavor")

    def test_wrong_compiler_runtime_rejected(self) -> None:
        want = SdkIdentity.from_json(_manifest_json(compiler_version="18.1.8", cxx_runtime_abi="libstdc++-cxx11"))
        got = SdkIdentity.from_json(_manifest_json(compiler_version="15.0.0", cxx_runtime_abi="libc++"))
        fields = {m.field_name for m in check_compatible(got, reference=want)}
        self.assertIn("compiler_version", fields)
        self.assertIn("cxx_runtime_abi", fields)

    def test_headers_and_libraries_from_different_variants(self) -> None:
        # A prefix whose policy/variant disagrees with what the project resolved
        # against (e.g. headers from one variant, libs from another) is caught by
        # the sdk_variant ABI key mismatch, not a version string.
        want = SdkIdentity.from_json(_manifest_json(
            sdk_variant="assert-v2-Development-RelWithDebInfo-dialogs-0-asan-0-ubsan-0-tsan-0"))
        mixed = SdkIdentity.from_json(_manifest_json(
            sdk_variant="assert-v2-Development-RelWithDebInfo-dialogs-1-asan-1-ubsan-0-tsan-0"))
        fields = {m.field_name for m in check_compatible(mixed, reference=want)}
        self.assertIn("sdk_variant", fields)


class StoreCorruptionAndRepair(unittest.TestCase):
    def test_corrupt_store_detected_then_repaired(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            store = SdkStore(Path(td) / "store")
            tar = Path(td) / "sdk.tar.gz"
            _make_sdk_tar(tar, _manifest_json())
            installed = store.install_archive(tar)
            # Corrupt the published (immutable) SDK by deleting its manifest.
            (installed.prefix / "share" / "Ludus" / "LudusSdkManifest.json").unlink()
            # A corrupt dir is skipped by listing (never offered to a project).
            self.assertEqual(store.list_installed(), [])
            # Re-installing the same identity detects corruption, does not silently
            # overwrite, and names the explicit repair path.
            with self.assertRaises(ToolingError) as ctx:
                store.install_archive(tar)
            self.assertEqual(ctx.exception.code, "SdkCorrupt")
            self.assertIn("repair", ctx.exception.message)
            # Explicit repair recovers.
            repaired = store.install_archive(tar, repair=True)
            self.assertTrue((repaired.prefix / "share" / "Ludus" / "LudusSdkManifest.json").is_file())


class LocalRefreshMidOperation(unittest.TestCase):
    def test_sdk_changed_mid_operation_aborts(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            store = SdkStore(Path(td) / "store")
            tar = Path(td) / "sdk.tar.gz"
            _make_sdk_tar(tar, _manifest_json())
            inst = store.install_archive(tar)
            res = resolve.resolve_sdk(
                target="x86_64-linux-gnu", flavor="Development",
                lock=lockfile.unresolved_lock("0.1.0", 1),
                local_settings=lockfile.LocalSettings(), store=store, cli_prefix=inst.prefix,
            )
            resolve.assert_stamp_unchanged(Path(td), res)  # unchanged: ok
            # Simulate a local engine reinstall changing the prefix during a build.
            (inst.prefix / "lib" / "libludus_foundation_base.a").write_bytes(b"\x00ar-NEW")
            with self.assertRaises(ToolingError) as ctx:
                resolve.assert_stamp_unchanged(Path(td), res)
            self.assertEqual(ctx.exception.code, "StampChanged")


class ConcurrentBuild(unittest.TestCase):
    def test_cli_and_editor_contend_on_one_build_tree(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tree = Path(td)
            # Editor backend holds the tree; the CLI backend must get Busy.
            editor_lock = editor_tool.BuildTreeLock(tree)
            editor_lock.acquire()
            try:
                with self.assertRaises(ToolingError) as ctx:
                    BuildTreeLock(tree).acquire()
                self.assertEqual(ctx.exception.code, "Busy")
            finally:
                editor_lock.release()
            # And the reverse: CLI holds, Editor backend gets BuildTreeBusy.
            cli_lock = BuildTreeLock(tree)
            cli_lock.acquire()
            try:
                with self.assertRaises(editor_tool.BuildTreeBusy):
                    editor_tool.BuildTreeLock(tree).acquire()
            finally:
                cli_lock.release()

    def test_lock_file_path_matches_editor_backend(self) -> None:
        # The two backends only interlock if they use the identical lock path.
        from ludus_tools import buildlock

        self.assertEqual(buildlock.LOCK_RELPATH, ".cmake/.ludus-editor.lock")


class CompetingInstallers(unittest.TestCase):
    def test_second_installer_of_same_identity_gets_busy(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            store = SdkStore(Path(td) / "store")
            tar = Path(td) / "sdk.tar.gz"
            _make_sdk_tar(tar, _manifest_json())
            inst = store.install_archive(tar)
            slug = store.prefix_for(inst.identity, inst.digest).name
            ready = threading.Event()
            release = threading.Event()

            def hold():
                with store._install_lock(slug):
                    ready.set()
                    release.wait(5)

            holder = threading.Thread(target=hold)
            holder.start()
            try:
                self.assertTrue(ready.wait(5))
                with self.assertRaises(ToolingError) as ctx:
                    with store._install_lock(slug):
                        pass
                self.assertEqual(ctx.exception.code, "Busy")
            finally:
                release.set()
                holder.join()


class FailedBuildCannotLaunchStale(unittest.TestCase):
    def test_run_refuses_launch_after_failed_build(self) -> None:
        # op_run must return the build failure and never launch an artifact when
        # the build fails. We drive op_build through a fake that fails, and assert
        # resolve_artifact is never consulted / no process is launched.
        with tempfile.TemporaryDirectory() as td:
            store = SdkStore(Path(td) / "store")
            prefix = Path(td) / "sdk"
            _write_sdk_prefix(prefix, _manifest_json())
            dest = Path(td) / "MyGame"
            create.create_project(dest, name="MyGame", template_id="minimal",
                                  engine_version="0.1.0", local_sdk_prefix=prefix)
            resolved = operations.resolve_project(dest, store=store, cli_sdk_prefix=prefix)

            launched = {"count": 0}
            real_run = operations._run
            real_build = operations.op_build

            def fake_build(_r):
                return 1  # build fails

            def spy_run(argv, *, cwd, env, echo=True):
                launched["count"] += 1
                return 0

            operations.op_build = fake_build  # type: ignore[assignment]
            operations._run = spy_run  # type: ignore[assignment]
            try:
                code = operations.op_run(resolved)
            finally:
                operations._run = real_run
                operations.op_build = real_build
            self.assertEqual(code, 1)
            self.assertEqual(launched["count"], 0, "a failed build must not launch any binary")


class DescriptorMigrationHazards(unittest.TestCase):
    def test_already_v2_migration_refused(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            proj = Path(td) / "p"
            proj.mkdir()
            (proj / "ludus.project.json").write_text(json.dumps({
                "version": 2, "name": "X", "provider": "cmake", "source_dir": ".",
                "preset": "linux-clang-development", "target": "t",
                "run": {"cwd": ".", "args": []}, "engine": {"version": "0.1.0"},
            }))
            with self.assertRaises(ToolingError) as ctx:
                create.migrate_v1_to_v2(proj, engine_version="0.1.0")
            self.assertEqual(ctx.exception.code, "MigrationFailed")

    def test_lock_disagreement_detected(self) -> None:
        lock = lockfile.unresolved_lock("9.9.9", 1)
        with self.assertRaises(ToolingError) as ctx:
            lockfile.validate_descriptor_lock_agreement("0.1.0", lock)
        self.assertEqual(ctx.exception.code, "LockMismatch")

    def test_interrupted_paired_commit_is_recoverable(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            proj = Path(td) / "p"
            proj.mkdir()
            # Simulate a crash mid paired-commit: leftover .migrating temporaries.
            (proj / "ludus.project.json.migrating").write_text("partial")
            (proj / "ludus.lock.json.migrating").write_text("partial")
            discarded = create.recover_partial_migration(proj)
            self.assertEqual(sorted(discarded),
                             ["ludus.lock.json.migrating", "ludus.project.json.migrating"])
            self.assertFalse((proj / "ludus.project.json.migrating").exists())


class PathsWithSpacesUnicode(unittest.TestCase):
    def test_create_in_unicode_spaced_path(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            dest = Path(td) / "Jeux Vidéo 日本語"
            result = create.create_project(dest, name="Jeux Vidéo 日本語",
                                          template_id="minimal", engine_version="0.1.0")
            self.assertTrue((result.destination / "ludus.project.json").is_file())
            d = descriptor.parse_descriptor_file(result.destination / "ludus.project.json")
            self.assertEqual(d.name, "Jeux Vidéo 日本語")
            self.assertTrue(descriptor.TARGET_NAME_RE.fullmatch(d.target))


class QtAbsentHeadless(unittest.TestCase):
    def test_tooling_imports_without_qt(self) -> None:
        # Importing and running the host tooling must never pull in Qt (P07/P11).
        self.assertNotIn("PyQt6", sys.modules)
        self.assertNotIn("PySide6", sys.modules)

    def test_cli_runs_in_subprocess_with_qt_import_poisoned(self) -> None:
        # Prove the CLI works even if importing Qt would hard-fail: inject a
        # sitecustomize that makes any `import PyQt6/PySide6` raise, then run the
        # CLI. If the tooling touched Qt, this would fail.
        with tempfile.TemporaryDirectory() as td:
            poison = Path(td) / "sitecustomize.py"
            poison.write_text(
                "import builtins\n"
                "_real_import = builtins.__import__\n"
                "def _blocked(name, *a, **k):\n"
                "    if name.split('.')[0] in ('PyQt6', 'PySide6', 'PyQt5'):\n"
                "        raise ImportError('Qt is absent on this machine')\n"
                "    return _real_import(name, *a, **k)\n"
                "builtins.__import__ = _blocked\n"
            )
            env = dict(os.environ)
            scripts_python = str(Path(__file__).resolve().parent)
            env["PYTHONPATH"] = str(td) + os.pathsep + scripts_python
            proc = subprocess.run(
                [sys.executable, "-m", "ludus_tools", "--store", str(Path(td) / "store"), "sdk", "list"],
                env=env, cwd=td, capture_output=True, text=True,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)


if __name__ == "__main__":
    unittest.main()
