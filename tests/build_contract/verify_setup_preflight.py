"""Exercise real CMake configuration before compiler/dependency discovery."""
import hashlib
import json
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
        shutil.copy2(SOURCE / "cmake/LudusDependencies.cmake", self.root / "cmake")
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
                if relative.startswith("out/conan/"):
                    marker = toolchain.parent / ".ludus-bootstrap.json"
                    paths = {"sdk": self.root / "out/host-tools/macos-sdk",
                             "libcxx": self.root / "out/host-tools/libcxx-include",
                             "compiler": self.root / "out/host-tools/bin/clang++"}
                    for path in paths.values():
                        path.mkdir(parents=True, exist_ok=True)
                    identity = {key: str(path.resolve()) for key, path in paths.items()}
                    for key, path in (("sdk_settings", paths["sdk"] / "SDKSettings.json"),
                                      ("libcxx_config", paths["libcxx"] / "__config")):
                        path.write_text("fixture")
                        identity[key] = hashlib.sha256(path.read_bytes()).hexdigest()
                    marker.write_text(json.dumps({"version": 2, "macos_toolchain": identity}))
                before = toolchain.read_bytes()
                result = self.configure(toolchain)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertEqual(toolchain.read_bytes(), before)
                self.assertFalse((self.root / "out/init/options.json").exists())
                if relative.startswith("out/conan/"):
                    stamp = self.root / "out/build/macos-clang-debug/.ludus-macos-toolchain.json"
                    self.assertEqual(json.loads(stamp.read_text()), identity)
                    stamp.unlink()
                    with (self.root / "CMakeLists.txt").open("a") as source:
                        source.write('message(FATAL_ERROR "later configure failure")\n')
                    failed = self.configure(toolchain)
                    self.assertNotEqual(failed.returncode, 0)
                    self.assertIn("later configure failure", failed.stdout)
                    self.assertFalse(stamp.exists())
                    source = self.root / "CMakeLists.txt"
                    source.write_text(source.read_text().replace(
                        'message(FATAL_ERROR "later configure failure")\n', ''))

    def test_no_toolchain_requires_initialization(self):
        (self.root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.29)\n'
            'include(cmake/EngineSetupPreflight.cmake)\nproject(Unprepared NONE)\n')
        result = subprocess.run([CMAKE, "-S", str(self.root), "-B", str(self.root / "build")],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Initialize this checkout", result.stdout)
        self.assertIn("./init.sh", result.stdout)
        self.assertNotIn("compiler identification", result.stdout)

    def test_existing_generated_toolchain_requires_current_init_record(self):
        (self.root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.29)\n'
            'include(cmake/EngineSetupPreflight.cmake)\nproject(Unprepared NONE)\n')
        toolchain = self.root / "out/conan/linux-clang-development/conan_toolchain.cmake"
        toolchain.parent.mkdir(parents=True)
        toolchain.write_text("# generated fixture\n")
        marker = toolchain.parent / ".ludus-bootstrap.json"
        for content in (None, "not json", '{"version": 1}'):
            if content is not None:
                marker.write_text(content)
            result = self.configure(toolchain, "linux-clang-development")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("initialization record", result.stdout)
            self.assertIn("--preset linux-clang-development", result.stdout)
        marker.write_text('{"version": 2}')
        result = self.configure(toolchain, "linux-clang-development")
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_mac_sdk_retarget_and_metadata_change_require_reinitialization(self):
        (self.root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.29)\n'
            'include(cmake/EngineSetupPreflight.cmake)\nproject(Prepared NONE)\n')
        toolchain = self.root / "out/conan/macos-clang-debug/conan_toolchain.cmake"
        toolchain.parent.mkdir(parents=True)
        toolchain.write_text("# generated fixture\n")
        sdk = self.root / "SDK with spaces"
        moved = self.root / "another SDK"
        for path in (sdk, moved):
            path.mkdir()
            (path / "SDKSettings.json").write_text("fixture")
        sdk_link = self.root / "out/host-tools/macos-sdk"
        sdk_link.parent.mkdir(parents=True)
        sdk_link.symlink_to(sdk, target_is_directory=True)
        libcxx = self.root / "out/host-tools/libcxx-include"
        libcxx.mkdir()
        (libcxx / "__config").write_text("fixture")
        compiler = self.root / "out/host-tools/bin/clang++"
        compiler.parent.mkdir()
        compiler.touch()
        identity = {"sdk": str(sdk.resolve()), "libcxx": str(libcxx.resolve()),
                    "compiler": str(compiler.resolve()),
                    "sdk_settings": hashlib.sha256(b"fixture").hexdigest(),
                    "libcxx_config": hashlib.sha256(b"fixture").hexdigest()}
        (toolchain.parent / ".ludus-bootstrap.json").write_text(
            json.dumps({"version": 2, "macos_toolchain": identity}))
        self.assertEqual(self.configure(toolchain).returncode, 0)
        sdk_link.unlink()
        sdk_link.symlink_to(moved, target_is_directory=True)
        result = self.configure(toolchain)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("macOS sdk is missing, moved or not validated", result.stdout)
        sdk_link.unlink()
        sdk_link.symlink_to(sdk, target_is_directory=True)
        (sdk / "SDKSettings.json").write_text("changed")
        result = self.configure(toolchain)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Initialized input changed", result.stdout)
        self.assertIn("./init.sh", result.stdout)

    def test_missing_custom_toolchain_keeps_cmake_diagnostic(self):
        result = self.configure(self.root / "custom.cmake")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not find toolchain file", result.stdout)
        self.assertNotIn("Run init before configuring CMake", result.stdout)


if __name__ == "__main__":
    unittest.main()
