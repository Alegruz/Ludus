"""Exercise real CMake configuration before compiler/dependency discovery."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(sys.argv[1]).resolve()
CMAKE = sys.argv[2]
NINJA = sys.argv[3]
del sys.argv[1:]


class SetupPreflightTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "checkout with spaces"
        (self.root / "cmake").mkdir(parents=True)
        shutil.copy2(SOURCE / "cmake/EngineSetupPreflight.cmake", self.root / "cmake")
        shutil.copy2(SOURCE / "CMakeLists.txt", self.root)

    def configure(self, toolchain, preset="macos-clang-debug"):
        return subprocess.run(
            [CMAKE, "-S", str(self.root), "-B", str(self.root / "out/build" / preset),
             "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={NINJA}",
             f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", "-DLUDUS_BUILD_TESTS=ON"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)

    def test_missing_native_toolchain_has_preset_specific_init_command(self):
        for preset in ("linux-clang-development", "macos-clang-debug"):
            with self.subTest(preset=preset):
                result = self.configure(self.root / "out/conan" / preset / "conan_toolchain.cmake", preset)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Run init before configuring CMake", result.stdout)
                self.assertIn(f"./init.sh --cli --preset {preset} --preset-only --locked", result.stdout)
                self.assertIn("--with-tests", result.stdout)
                self.assertNotIn("CMakeDetermineSystem.cmake", result.stdout)

    def test_relative_native_toolchain_also_reports_init(self):
        result = self.configure("out/conan/macos-clang-debug/conan_toolchain.cmake")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Run init before configuring CMake", result.stdout)

    def test_missing_web_toolchain_reports_web_init(self):
        result = self.configure(
            self.root / "out/host-tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake",
            "web-emscripten-release")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--preset web-emscripten-release --preset-only --locked", result.stdout)
        self.assertNotIn("--with-tests", result.stdout)

    def test_existing_toolchains_pass_without_preparing_or_rewriting_files(self):
        # No compiler is needed for this fixture; reaching project() proves the
        # preflight accepts prepared/custom toolchains without running setup.
        (self.root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.29)\n'
            'include(cmake/EngineSetupPreflight.cmake)\nproject(Prepared NONE)\n')
        for relative in ("out/conan/macos-clang-debug/conan_toolchain.cmake", "custom.cmake"):
            with self.subTest(toolchain=relative):
                toolchain = self.root / relative
                toolchain.parent.mkdir(parents=True, exist_ok=True)
                toolchain.write_text("# Caller-owned toolchain\n")
                before = toolchain.read_bytes()
                result = self.configure(toolchain)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertEqual(toolchain.read_bytes(), before)
                self.assertFalse((self.root / "out/init/options.json").exists())

    def test_missing_custom_toolchain_keeps_cmake_diagnostic(self):
        result = self.configure(self.root / "custom.cmake")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not find toolchain file", result.stdout)
        self.assertNotIn("Run init before configuring CMake", result.stdout)


if __name__ == "__main__":
    unittest.main()
