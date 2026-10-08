"""macOS Editor setup policy and Darwin process ownership regressions."""
from __future__ import annotations

import ctypes
import errno
import os
from pathlib import Path
import platform
import signal
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

import editor_launch
import editor_tool
import engine
import init_editor
import init_ui


class SetupTests(unittest.TestCase):
    def args(self, preset="macos-clang-development", *options):
        return engine.make_parser().parse_args(["init", "--cli", "--with-editor", preset, *options])

    def test_macos_native_options_and_host_mismatch(self):
        for machine in ("arm64", "x86_64"):
            with patch.object(platform, "system", return_value="Darwin"), patch.object(platform, "machine", return_value=machine):
                for preset in ("macos-clang-debug", "macos-clang-development", "macos-clang-asan-ubsan"):
                    init_editor.validate_editor_options(self.args(preset), engine)
                for preset in ("linux-clang-development", "macos-clang-release", "web-emscripten-development"):
                    with self.assertRaises(engine.EngineError):
                        init_editor.validate_editor_options(self.args(preset), engine)
        self.assertIn("macos-clang-development", editor_launch.SUPPORTED_PRESETS)

    def test_existing_qt_uses_local_cache_without_installing(self):
        with tempfile.TemporaryDirectory() as temporary:
            qt = Path(temporary) / "Qt with spaces"
            (qt / "lib/cmake/Qt6").mkdir(parents=True)
            with patch.object(platform, "system", return_value="Darwin"), patch.object(platform, "machine", return_value="arm64"), \
                 patch.object(init_editor.shutil, "which", return_value="/homebrew/bin/brew"), \
                 patch.object(engine, "capture_command_quiet", return_value=(0, str(qt))), \
                 patch.object(engine, "run") as run, patch.object(engine, "cmake_configure") as configure, \
                 patch.object(engine, "cmake_build") as build, patch.object(init_editor.cmake_targets, "query_codemodel"):
                init_editor.setup_editor(self.args("macos-clang-development", "--no-system-install"), engine)
                run.assert_not_called()
                self.assertEqual(configure.call_args.args[2], ["-DLUDUS_BUILD_EDITOR=ON", f"-DQt6_DIR={qt}/lib/cmake/Qt6"])
                self.assertEqual(build.call_args.args[1], "macos-clang-development")

    def test_explicit_install_is_only_qtbase(self):
        with patch.object(platform, "system", return_value="Darwin"), patch.object(platform, "machine", return_value="arm64"), \
             patch.object(init_editor.shutil, "which", return_value="/brew"), \
             patch.object(engine, "capture_command_quiet", return_value=(1, "")), \
             patch.object(engine, "run") as run, patch.object(engine, "cmake_configure"), \
             patch.object(engine, "cmake_build"), patch.object(init_editor.cmake_targets, "query_codemodel"):
            init_editor.setup_editor(self.args(), engine)
            self.assertEqual(run.call_args.args[0], ["/brew", "install", "qtbase"])


@unittest.skipUnless(sys.platform == "darwin", "Darwin libc process API")
class ProcessTests(unittest.TestCase):
    def test_observation_retains_zombie_until_finalize(self):
        child = editor_tool.OwnedProcess([sys.executable, "-c", "raise SystemExit(23)"], "/tmp", dict(os.environ))
        try:
            deadline = time.monotonic() + 5
            while child.wait_exit_nowait() is None and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertEqual(child.wait_exit_nowait(), (23, None))
            self.assertEqual(child.wait_exit_nowait(), (23, None))
            self.assertIsNone(child._proc.returncode)
            report = child.finalize_normal()
            self.assertTrue(report.confirmed)
            self.assertEqual(report.exit_code, 23)
            with self.assertRaises(ChildProcessError):
                os.waitpid(child.pid, os.WNOHANG)
        finally:
            if child._proc.returncode is None:
                child.cleanup(cancelled=True)

    def test_stopped_child_is_observed_and_can_be_cleaned(self):
        from editor_process_macos import process_stopped
        child = editor_tool.OwnedProcess([sys.executable, "-c", "import time; time.sleep(60)"], "/tmp", dict(os.environ))
        try:
            os.kill(child.pid, signal.SIGSTOP)
            deadline = time.monotonic() + 5
            while not process_stopped(child.pid) and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertTrue(process_stopped(child.pid))
            os.kill(child.pid, signal.SIGCONT)
            self.assertTrue(child.cleanup(cancelled=True).confirmed)
        finally:
            if child._proc.returncode is None:
                os.kill(child.pid, signal.SIGCONT)
                child.cleanup(cancelled=True)

    def test_inventory_failure_and_truncation_are_unknown(self):
        import editor_process_macos as native
        for count in (0, -1, 65536, 3):
            with patch.object(native, "_listpids", return_value=count):
                self.assertIsNone(native.group_members(123))
        def unreadable(_kind, _leader, pids, _capacity):
            pids[0], pids[1] = 123, 456
            return 8
        with patch.object(native, "_listpids", side_effect=unreadable), patch.object(native, "_pidinfo", return_value=0):
            ctypes.set_errno(errno.EPERM)
            # No info is not a successful proof of absence.
            self.assertIsNone(native.group_members(123))

    def test_unknown_inventory_never_reaps_leader(self):
        child = editor_tool.OwnedProcess([sys.executable, "-c", "raise SystemExit(0)"], "/tmp", dict(os.environ))
        try:
            with patch.object(child, "_group_members", return_value=None):
                report = child.cleanup(cancelled=True)
            self.assertFalse(report.confirmed)
            self.assertIsNone(child._proc.returncode)
            self.assertTrue(child.finalize_normal().confirmed)
        finally:
            if child._proc.returncode is None:
                child.cleanup(cancelled=True)
            if child in editor_tool._UNKNOWN_PROCESS_OWNERS:
                editor_tool._UNKNOWN_PROCESS_OWNERS.remove(child)


if __name__ == "__main__":
    unittest.main()
