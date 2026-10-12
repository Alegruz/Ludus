"""Prepared-preset admission, read-only failures and preservation of custom IDE files."""
from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

import editor_setup as editor
import engine


class EditorSetupTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.preset = engine.HOST_PRESETS[1]

    def test_missing_selection_does_not_create_or_repair_files(self):
        with self.assertRaisesRegex(engine.EngineError, "setup missing"):
            editor.selected(self.root)
        self.assertEqual([], list(self.root.iterdir()))

    def test_repeated_selection_preserves_custom_settings_tasks_and_presets(self):
        for name in (".vscode/settings.json", ".vscode/tasks.json", "CMakeUserPresets.json"):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('{"custom": "preserve byte for byte"}\n')
        before = {str(p): p.read_bytes() for p in self.root.rglob("*.json")}
        with patch.object(editor, "validate", return_value=Path("clangd")), contextlib.redirect_stdout(io.StringIO()):
            editor.select(self.root, self.preset)
            first = editor.selection_path(self.root).read_bytes()
            editor.select(self.root, self.preset)
            self.assertEqual(first, editor.selection_path(self.root).read_bytes())
        self.assertEqual(before, {name: Path(name).read_bytes() for name in before})

    def test_failed_selection_keeps_previous_selection(self):
        with patch.object(editor, "validate", return_value=Path("clangd")), contextlib.redirect_stdout(io.StringIO()):
            editor.select(self.root, self.preset)
        before = editor.selection_path(self.root).read_bytes()
        with patch.object(editor, "validate", side_effect=engine.EngineError("missing SDK")), \
                self.assertRaisesRegex(engine.EngineError, "missing SDK"):
            editor.select(self.root, engine.HOST_PRESETS[0])
        self.assertEqual(before, editor.selection_path(self.root).read_bytes())

    def test_moved_checkout_rejects_old_selection_before_tool_execution(self):
        path = editor.selection_path(self.root)
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps({"version": 1, "root": "/old/checkout", "preset": self.preset}))
        with patch.object(editor, "validate") as validate, self.assertRaisesRegex(engine.EngineError, "moved"):
            editor.selected(self.root)
        validate.assert_not_called()

    def test_wrong_host_preset_is_rejected_without_bootstrap(self):
        with patch.object(engine, "ensure_bootstrap_for_preset") as bootstrap, \
                self.assertRaisesRegex(engine.EngineError, "native preset"):
            editor.validate(self.root, "web-emscripten-development")
        bootstrap.assert_not_called()

    def test_moved_configure_cache_is_rejected_without_reconfiguration(self):
        cache = engine.build_dir_for_preset(self.root, self.preset) / "CMakeCache.txt"
        cache.parent.mkdir(parents=True)
        cache.write_text("CMAKE_HOME_DIRECTORY:INTERNAL=/old/source\n")
        with patch.object(engine, "ensure_bootstrap_for_preset"), \
                self.assertRaisesRegex(engine.EngineError, "moved configure cache"):
            editor.validate(self.root, self.preset)
        self.assertIn("/old/source", cache.read_text())

    def test_cmake_conditions_can_hide_parseable_presets(self):
        executable = shutil.which("cmake") or str(engine.managed_executable(engine.repo_root(), "cmake"))
        if not Path(executable).is_file():
            self.skipTest("Pinned CMake unavailable")
        presets = {"version": 6, "configurePresets": [{"name": self.preset, "generator": "Ninja", "condition": False}],
                   "buildPresets": [{"name": self.preset, "configurePreset": self.preset, "condition": False}],
                   "testPresets": [{"name": self.preset, "configurePreset": self.preset, "condition": False}]}
        (self.root / "CMakePresets.json").write_text(json.dumps(presets))
        cache = engine.build_dir_for_preset(self.root, self.preset) / "CMakeCache.txt"
        cache.parent.mkdir(parents=True)
        cache.write_text(f"CMAKE_HOME_DIRECTORY:INTERNAL={self.root}\n")
        for tool in (engine.clang_cxx(self.root), engine.ninja(self.root)):
            tool.parent.mkdir(parents=True, exist_ok=True)
            tool.symlink_to("/usr/bin/true")
        engine.cmake(self.root).symlink_to(executable)
        clangd = engine.host_tools_bin_dir(self.root) / "clangd"
        clangd.write_text("#!/bin/sh\necho 'clangd version 18.1.8'\n")
        clangd.chmod(0o755)
        with patch.object(engine, "ensure_bootstrap_for_preset"), \
                patch.object(engine, "cmake_cache_repair_reasons", return_value=[]), \
                patch.object(engine, "executable_version", return_value="clang version 18.1.8"), \
                patch.object(editor, "inventory") as inventory, \
                self.assertRaisesRegex(engine.EngineError, "configure preset .* unavailable"):
            editor.validate(self.root, self.preset)
        inventory.assert_not_called()


if __name__ == "__main__":
    unittest.main()
