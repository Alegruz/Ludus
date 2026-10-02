"""Setup routing tests without downloads, package installation or a desktop."""
from __future__ import annotations

import contextlib
import io
import os
import unittest
from unittest.mock import patch

import engine
import init_editor
import init_ui


class InitTests(unittest.TestCase):
    def args(self, *options):
        return engine.make_parser().parse_args(["init", *options])

    def test_persona_defaults_and_explicit_overrides(self):
        for persona, preset, single, validation in (
            ("contributor", engine.DEFAULT_PRESET, False, False),
            ("application", engine.DEFAULT_PRESET, True, False),
            ("browser", "web-emscripten-development", True, False),
            ("validation", engine.DEFAULT_PRESET, False, True),
        ):
            with self.subTest(persona=persona):
                args = init_ui.apply_defaults(self.args("--persona", persona))
                self.assertEqual((args.preset, args.preset_only, args.validate), (preset, single, validation))
        args = init_ui.apply_defaults(self.args("--persona", "application", "linux-clang-debug", "--all-presets"))
        self.assertEqual(args.preset, "linux-clang-debug")
        self.assertFalse(args.preset_only)
        self.assertTrue(args.all_presets)

    def test_browser_defaults_do_not_prepare_native_dependencies(self):
        args = init_ui.prepare_init(self.args("--cli", "web-emscripten-release"), engine)
        self.assertTrue(args.preset_only)
        self.assertFalse(args.all_presets)
        with patch("web_build.command", return_value=0) as browser, patch.object(engine, "command_init") as native:
            self.assertEqual(engine.main(["init", "--cli", "--persona", "browser"]), 0)
        browser.assert_called_once()
        native.assert_not_called()

    def test_invalid_combinations_fail_before_window_or_installation(self):
        for options in (
            ("--all-presets", "--preset-only"),
            ("--persona", "browser", "--validate"),
            ("--persona", "browser", "--ci"),
            ("--persona", "browser", "--all-presets"),
            ("--persona", "browser", "--with-rad-debugger"),
            ("--persona", "browser", "--with-editor"),
            ("linux-clang-release", "--with-editor"),
            ("invalid-preset",),
        ):
            with self.subTest(options=options), patch.object(init_ui, "select_options") as gui:
                with self.assertRaises(engine.EngineError):
                    init_ui.prepare_init(self.args("--gui", *options), engine)
                gui.assert_not_called()

    def test_default_gui_and_noninteractive_routing(self):
        with patch.dict(os.environ, {"DISPLAY": ":test"}, clear=True), \
                patch("sys.stdin.isatty", return_value=True), patch("sys.stdout.isatty", return_value=True):
            self.assertTrue(init_ui.wants_gui(self.args()))
            self.assertFalse(init_ui.wants_gui(self.args("--cli")))
            self.assertFalse(init_ui.wants_gui(self.args("--ci")))
            with patch.dict(os.environ, {"CI": "true"}):
                self.assertFalse(init_ui.wants_gui(self.args()))
            with patch("sys.stdin.isatty", return_value=False):
                self.assertFalse(init_ui.wants_gui(self.args()))
                self.assertTrue(init_ui.wants_gui(self.args("--gui")))
        with patch.dict(os.environ, {}, clear=True), patch("sys.platform", "linux"):
            self.assertFalse(init_ui.wants_gui(self.args()))

    def test_cancel_does_not_install(self):
        with patch.object(init_ui, "select_options", return_value=None), \
                patch.object(engine, "command_init") as install, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(engine.main(["init", "--gui"]), 0)
        install.assert_not_called()

    def test_explicit_gui_failure_does_not_install(self):
        with patch.object(init_ui, "select_options", side_effect=init_ui.GuiUnavailable("no display")), \
                patch.object(engine, "command_init") as install, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(engine.main(["init", "--gui"]), 1)
        install.assert_not_called()

    def test_default_gui_failure_falls_back_to_same_cli_options(self):
        with patch.object(init_ui, "wants_gui", return_value=True), \
                patch.object(init_ui, "select_options", side_effect=init_ui.GuiUnavailable("no Tk")), \
                contextlib.redirect_stderr(io.StringIO()) as output:
            args = init_ui.prepare_init(self.args("--no-system-install", "--with-rad-debugger"), engine)
        self.assertTrue(args.no_system_install)
        self.assertTrue(args.with_rad_debugger)
        self.assertIn("Continuing with CLI", output.getvalue())

    def test_cli_never_loads_window_and_passes_existing_flags(self):
        with patch.object(init_ui, "select_options") as gui, patch.object(engine, "command_init", return_value=0) as install:
            self.assertEqual(engine.main(["init", "--cli", "--validate", "--skip-sdk", "--no-system-install"]), 0)
        gui.assert_not_called()
        args = install.call_args.args[0]
        self.assertEqual(args.preset, engine.DEFAULT_PRESET)
        self.assertTrue(args.validate and args.skip_sdk and args.no_system_install)

    def test_editor_is_optional_and_explicitly_selectable(self):
        self.assertFalse(self.args().with_editor)
        self.assertFalse(self.args("--no-editor").with_editor)
        self.assertTrue(self.args("--with-editor").with_editor)
        with patch.object(init_editor.platform, "system", return_value="Darwin"):
            with self.assertRaisesRegex(engine.EngineError, "Linux x64"):
                init_ui.prepare_init(self.args("--cli", "--with-editor"), engine)


class EditorSetupTests(unittest.TestCase):
    def args(self, *options):
        return init_ui.apply_defaults(engine.make_parser().parse_args(["init", "--cli", "--with-editor", *options]))

    def test_existing_packages_are_not_installed_again(self):
        with patch.object(engine, "host_supports_apt_install", return_value=True), \
                patch.object(engine, "capture_command_quiet", return_value=(0, "install ok installed")), \
                patch.object(engine, "run") as run, patch.object(init_editor.cmake_targets, "query_codemodel") as query, \
                patch.object(engine, "cmake_configure") as configure, patch.object(engine, "cmake_build") as build:
            init_editor.setup_editor(self.args(), engine)
        run.assert_not_called()
        self.assertEqual(query.call_args.args[1], "ludus-editor")
        self.assertEqual(configure.call_args.args[2], ["-DLUDUS_BUILD_EDITOR=ON"])
        self.assertEqual(build.call_args.args[2], ["--target", "ludus_editor"])

    def test_installs_only_missing_opted_in_packages(self):
        with patch.object(engine, "host_supports_apt_install", return_value=True), \
                patch.object(engine, "capture_command_quiet", side_effect=[(0, "install ok installed"), (1, "missing")]), \
                patch.object(engine, "sudo_command_prefix", return_value=["sudo"]), \
                patch.object(engine, "run") as run, patch.object(init_editor.cmake_targets, "query_codemodel"), \
                patch.object(engine, "cmake_configure"), patch.object(engine, "cmake_build"):
            init_editor.setup_editor(self.args(), engine)
        self.assertEqual([call.args[0] for call in run.call_args_list],
                         [["sudo", "apt-get", "update"], ["sudo", "apt-get", "install", "-y", "qt6-wayland"]])

    def test_no_system_install_uses_existing_qt_and_propagates_configure_failure(self):
        with patch.object(engine, "capture_command_quiet") as query, patch.object(engine, "run") as run, \
                patch.object(init_editor.cmake_targets, "query_codemodel"), \
                patch.object(engine, "cmake_configure", side_effect=engine.EngineError("Qt not found")), \
                patch.object(engine, "cmake_build") as build:
            with self.assertRaisesRegex(engine.EngineError, "Qt not found"):
                init_editor.setup_editor(self.args("--no-system-install"), engine)
        query.assert_not_called()
        run.assert_not_called()
        build.assert_not_called()

    def test_init_calls_editor_setup_only_when_selected(self):
        for enabled in (False, True):
            with self.subTest(enabled=enabled), contextlib.ExitStack() as stack:
                args = init_ui.apply_defaults(engine.make_parser().parse_args([
                    "init", "--cli", "--no-system-install", "--with-editor" if enabled else "--no-editor"]))
                for name in ("check_current_python", "configure_system_tool_shims", "prepare_conan_artifacts",
                             "repair_existing_cmake_caches", "command_install_hooks"):
                    stack.enter_context(patch.object(engine, name))
                stack.enter_context(patch.object(engine, "load_tool_versions", return_value={"minimum": {"python": "3.10"}}))
                stack.enter_context(patch.object(engine, "command_doctor", return_value=0))
                setup = stack.enter_context(patch.object(init_editor, "setup_editor"))
                stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
                self.assertEqual(engine.command_init(args), 0)
                self.assertEqual(setup.call_count, int(enabled))


@unittest.skipUnless(os.environ.get("LUDUS_TEST_GUI") == "1", "requires a desktop display")
class WindowTests(unittest.TestCase):
    def select(self, action, *options):
        import tkinter as tk
        real_tk = tk.Tk
        errors = []

        def create_window():
            window = real_tk()

            def on_error(*error):
                errors.append(error)
                window.destroy()

            window.report_callback_exception = on_error

            def drive():
                def descendants(widget):
                    children = widget.winfo_children()
                    return children + [child for widget in children for child in descendants(widget)]
                action(window, descendants(window))

            window.after(100, drive)
            return window

        args = init_ui.apply_defaults(engine.make_parser().parse_args(["init", "--gui", *options]))
        with patch.object(tk, "Tk", side_effect=create_window):
            result = init_ui.select_options(args, engine)
        self.assertEqual(errors, [])
        return result

    def test_initialize_preserves_supplied_options(self):
        def accept(window, widgets):
            button = next(widget for widget in widgets if widget.winfo_class() == "TButton"
                          and widget.cget("text") == "Initialize")
            self.assertLessEqual(button.winfo_rootx() + button.winfo_width(),
                                 window.winfo_rootx() + window.winfo_width())
            self.assertLessEqual(button.winfo_rooty() + button.winfo_height(),
                                 window.winfo_rooty() + window.winfo_height())
            button.invoke()

        result = self.select(accept, "linux-clang-debug", "--preset-only", "--no-system-install",
                             "--with-rad-debugger", "--with-editor", "--validate", "--skip-sdk")
        self.assertEqual(result.preset, "linux-clang-debug")
        self.assertTrue(result.preset_only and result.no_system_install and result.with_rad_debugger)
        self.assertTrue(result.validate and result.skip_sdk)
        self.assertTrue(result.with_editor)

    def test_browser_workflow_disables_native_options(self):
        def accept(window, widgets):
            workflow = next(widget for widget in widgets if widget.winfo_class() == "TCombobox")
            workflow.set("Browser developer")
            workflow.event_generate("<<ComboboxSelected>>")
            window.update_idletasks()
            next(widget for widget in widgets if widget.winfo_class() == "TButton"
                 and widget.cget("text") == "Initialize").invoke()

        result = self.select(accept, "--with-rad-debugger", "--with-editor", "--validate")
        self.assertEqual(result.preset, "web-emscripten-development")
        self.assertTrue(result.preset_only)
        self.assertFalse(result.all_presets or result.with_rad_debugger or result.with_editor or result.validate or result.ci)

    def test_close_cancels(self):
        self.assertIsNone(self.select(lambda window, _widgets: window.destroy()))


if __name__ == "__main__":
    unittest.main()
