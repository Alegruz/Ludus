"""Exercise locked dependency preparation and bounded analysis failure handling."""
from __future__ import annotations

import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

import engine


class LockedSetupTests(unittest.TestCase):
    def test_locked_setup_preserves_lock_and_prepares_only_selected_preset(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
            root = Path(temporary)
            lock = root / "conan.lock"
            lock.write_bytes(b'{"version": "0.5", "requires": []}\n')
            before = lock.read_bytes()
            for name in ("check_current_python", "create_or_update_venv", "install_managed_tools",
                         "validate_managed_tools", "configure_system_tool_shims", "validate_required_system_tools",
                         "export_local_recipes", "run"):
                stack.enter_context(patch.object(engine, name))
            stack.enter_context(patch.object(engine, "install_conan_profile", return_value=root / "profile"))
            regenerate = stack.enter_context(patch.object(engine, "create_conan_lock"))
            install = stack.enter_context(patch.object(engine, "conan_install_for_preset"))
            engine.prepare_conan_artifacts(root, {"minimum": {"python": "3.10"}},
                                           (engine.DEFAULT_PRESET,), locked=True)
            regenerate.assert_not_called()
            install.assert_called_once_with(root, root / "profile", engine.DEFAULT_PRESET, locked=True)
            self.assertEqual(lock.read_bytes(), before)

    def test_missing_lock_fails_before_tools_or_network(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(engine, "run") as run, \
                patch.object(engine, "create_or_update_venv") as setup:
            with self.assertRaisesRegex(engine.EngineError, "committed conan.lock"):
                engine.prepare_conan_artifacts(Path(temporary), {}, (engine.DEFAULT_PRESET,), locked=True)
            setup.assert_not_called()
            run.assert_not_called()

    def test_locked_install_does_not_allow_unlocked_dependencies(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(engine, "run") as run, \
                patch.object(engine, "tool_env", return_value={}):
            root = Path(temporary)
            engine.conan_install_for_preset(root, root / "profile", engine.DEFAULT_PRESET, locked=True)
            command = run.call_args.args[0]
            self.assertIn("--lockfile", command)
            self.assertNotIn("--lockfile-partial", command)
            self.assertEqual(command[command.index("--lockfile") + 1], str(root / "conan.lock"))


class AnalysisTests(unittest.TestCase):
    def test_concurrency_is_bounded_and_all_failures_are_reported(self):
        barrier = threading.Barrier(2)
        mutex = threading.Lock()
        active = 0
        peak = 0
        visited = []

        def analyze(command, **kwargs):
            nonlocal active, peak
            with mutex:
                active += 1
                peak = max(peak, active)
                visited.append(command[1])
            barrier.wait(timeout=5)
            with mutex:
                active -= 1
            return subprocess.CompletedProcess(command, 1 if command[1] in ("0", "3") else 0,
                                               stdout=f"diagnostic {command[1]}\n")

        output = io.StringIO()
        with patch.dict(os.environ, {"LUDUS_TIDY_JOBS": "2"}), \
                patch.object(engine.subprocess, "run", side_effect=analyze), contextlib.redirect_stdout(output):
            with self.assertRaisesRegex(engine.EngineError, "2 translation unit"):
                engine.run_analysis_commands(Path.cwd(), [["tidy", str(i)] for i in range(4)])
        self.assertEqual(peak, 2)
        self.assertCountEqual(visited, ["0", "1", "2", "3"])
        diagnostics = [line for line in output.getvalue().splitlines() if line.startswith("diagnostic")]
        self.assertEqual(diagnostics, [f"diagnostic {i}" for i in range(4)])

    def test_invalid_worker_count_fails_without_launching_analysis(self):
        for value in ("0", "-1", "two"):
            with self.subTest(value=value), patch.dict(os.environ, {"LUDUS_TIDY_JOBS": value}), \
                    patch.object(engine.subprocess, "run") as run:
                with self.assertRaisesRegex(engine.EngineError, "positive integer"):
                    engine.run_analysis_commands(Path.cwd(), [["tidy", "source.cpp"]])
                run.assert_not_called()

    def test_real_process_diagnostics_survive_a_failed_command(self):
        commands = [
            [sys.executable, "-c", "import sys; print('analysis error', file=sys.stderr); sys.exit(2)"],
            [sys.executable, "-c", "print('analysis completed')"],
        ]
        output = io.StringIO()
        with patch.dict(os.environ, {"LUDUS_TIDY_JOBS": "2"}), contextlib.redirect_stdout(output):
            with self.assertRaisesRegex(engine.EngineError, "1 translation unit"):
                engine.run_analysis_commands(Path.cwd(), commands)
        self.assertIn("analysis error\n", output.getvalue())
        self.assertIn("analysis completed\n", output.getvalue())
        self.assertIn("Analysis exited with code 2", output.getvalue())

    def test_missing_analyzer_reports_actionable_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(engine.EngineError, "Cannot launch analysis"):
                engine.run_analysis_commands(Path(temporary), [[Path(temporary) / "missing-analyzer"]])

    def test_default_worker_count_is_serial(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(engine, "ThreadPoolExecutor") as pool:
            pool.return_value.__enter__.return_value.map.return_value = []
            engine.run_analysis_commands(Path.cwd(), [["tidy", "source.cpp"]])
            pool.assert_called_once_with(max_workers=1)


if __name__ == "__main__":
    unittest.main()
