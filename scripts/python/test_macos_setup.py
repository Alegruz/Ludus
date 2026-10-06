"""Host selection regressions for the compile-only macOS slice."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import engine
import init_ui


class MacosSetupTests(unittest.TestCase):
    def test_native_profiles_distinguish_mac_architectures(self):
        for arch in ("arm64", "x86_64"):
            with self.subTest(arch=arch), patch.object(engine.platform, "system", return_value="Darwin"), \
                    patch.object(engine.platform, "machine", return_value=arch):
                self.assertEqual(engine.native_profile_name(), "macos-clang-" + arch)
        with patch.object(engine.platform, "system", return_value="Darwin"), \
                patch.object(engine.platform, "machine", return_value="unsupported"):
            with self.assertRaises(engine.EngineError):
                engine.native_profile_name()
        with patch.object(engine.platform, "system", return_value="Linux"):
            self.assertEqual(engine.native_profile_name(), "linux-clang-x86_64")

    def test_profile_install_is_repeatable_and_tracks_changes(self):
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(engine.platform, "system", return_value="Darwin"), \
                patch.object(engine.platform, "machine", return_value="arm64"):
            root = Path(temporary)
            source = root / "config/conan/profiles/macos-clang-arm64"
            source.parent.mkdir(parents=True)
            source.write_text("first\n")
            installed = engine.install_conan_profile(root)
            self.assertTrue(installed.read_text().startswith("first\n"))
            self.assertIn("tools.apple:sdk_path=", installed.read_text())
            source.write_text("updated\n")
            self.assertTrue(engine.install_conan_profile(root).read_text().startswith("updated\n"))

    def test_sdk_selection_tracks_clang_and_rejects_missing_sdk(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler = root / "toolchain/bin/clang++"
            compiler.parent.mkdir(parents=True)
            compiler.touch()
            (root / "toolchain/include/c++/v1").mkdir(parents=True)
            selected = patch.object(engine, "clang_cxx", return_value=compiler)
            selected.start()
            self.addCleanup(selected.stop)
            first = root / "SDK with spaces"
            second = root / "relocated SDK"
            first.mkdir()
            second.mkdir()
            with patch.dict(engine.os.environ, {"SDKROOT": ""}), \
                    patch.object(engine, "capture_command_quiet", return_value=(0, f'"-isysroot" "{first}"')):
                engine.configure_macos_sdk(root)
            link = root / "out/host-tools/macos-sdk"
            self.assertEqual(link.resolve(), first.resolve())
            with patch.dict(engine.os.environ, {"SDKROOT": str(second)}):
                engine.configure_macos_sdk(root)
                engine.configure_macos_sdk(root)
            self.assertEqual(link.resolve(), second.resolve())
            with patch.dict(engine.os.environ, {"SDKROOT": str(root / "missing")}):
                with self.assertRaises(engine.EngineError):
                    engine.configure_macos_sdk(root)
            self.assertEqual(link.resolve(), second.resolve())

    def test_all_mac_profiles_have_build_types_and_host_defaults(self):
        for flavor in ("debug", "development", "profile", "release", "asan-ubsan"):
            self.assertIn("macos-clang-" + flavor, engine.PRESET_BUILD_TYPES)
        args = init_ui.apply_defaults(engine.make_parser().parse_args(["init", "--cli"]))
        self.assertEqual(args.preset, engine.DEFAULT_PRESET)
        self.assertTrue(all(name.startswith(engine.NATIVE_PRESET_PREFIX) for name in engine.HOST_PRESETS))
