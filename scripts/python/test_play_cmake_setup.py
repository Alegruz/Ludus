from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest

from ludus_tools.cmake_setup import inspect_project_setup, validate_project_presets
from ludus_tools.templates import ProjectInputs, get_template, render_files


class CMakeSetupTests(unittest.TestCase):
    def setUp(self) -> None:
        self.cmake = shutil.which("cmake")
        self.assertIsNotNone(self.cmake, "the pinned CMake executable must be available")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name)
        files = render_files(get_template("minimal"), ProjectInputs(
            name="setup-check", target="setup_check", engine_version="0.1.0", components=[]))
        for item in files:
            if item.relpath in ("CMakePresets.json", "CMakeLists.txt"):
                (self.project / item.relpath).write_text(item.content, encoding="utf-8")
        self.env = dict(os.environ)
        self.env["LUDUS_SDK_PREFIX"] = str(self.project / "relocated-sdk")

    def test_fresh_clone_has_selectable_configure_build_and_test_presets(self) -> None:
        self.assertFalse((self.project / "CMakeUserPresets.json").exists())
        for preset in ("linux-clang-debug", "linux-clang-development", "linux-clang-release"):
            with self.subTest(preset=preset):
                validate_project_presets(self.cmake, self.project, self.env, preset)

    def test_missing_test_preset_is_reported_without_configuring_or_writing(self) -> None:
        path = self.project / "CMakePresets.json"
        value = json.loads(path.read_text(encoding="utf-8"))
        del value["testPresets"]
        path.write_text(json.dumps(value), encoding="utf-8")
        before = path.read_bytes()
        with self.assertRaisesRegex(ValueError, "missing a selectable test preset"):
            validate_project_presets(self.cmake, self.project, self.env, "linux-clang-development")
        self.assertEqual(path.read_bytes(), before)
        self.assertFalse((self.project / "out").exists())

    def test_missing_or_moved_cmake_is_reported_without_repair(self) -> None:
        with self.assertRaisesRegex(ValueError, "cannot run the selected CMake"):
            validate_project_presets(str(self.project / "missing-cmake"), self.project,
                                     self.env, "linux-clang-development")
        self.assertFalse((self.project / "CMakeUserPresets.json").exists())
        self.assertFalse((self.project / "out").exists())

    def test_setup_inspection_is_read_only_and_reports_missing_sdk(self) -> None:
        ninja = shutil.which("ninja")
        self.assertIsNotNone(ninja)
        with self.assertRaisesRegex(ValueError, "SDK prefix is missing"):
            inspect_project_setup(self.cmake, ninja, self.project, self.project / "out/build",
                                  self.env, "linux-clang-development", sdk_prefix=self.env["LUDUS_SDK_PREFIX"])
        self.assertFalse((self.project / "out").exists())
        self.assertFalse((self.project / "CMakeUserPresets.json").exists())

    def test_stale_cache_is_reported_without_reconfigure(self) -> None:
        ninja = shutil.which("ninja")
        self.assertIsNotNone(ninja)
        # This test checks cache comparison, not compiler execution. A local
        # fixture keeps it independent of the host's LLVM installation path.
        compiler = self.project / "tools with spaces" / "clang++"
        compiler.parent.mkdir()
        compiler.touch()
        prefix = self.project / "relocated-sdk"
        prefix.mkdir()
        build = self.project / "out/build/linux-clang-development"
        build.mkdir(parents=True)
        cache = build / "CMakeCache.txt"
        cache.write_text(
            "CMAKE_GENERATOR:INTERNAL=Ninja\n"
            "CMAKE_MAKE_PROGRAM:FILEPATH=/old/tools/ninja\n"
            "CMAKE_CXX_COMPILER:FILEPATH=/old/tools/clang++\n"
            f"CMAKE_PREFIX_PATH:PATH={prefix}\n",
            encoding="utf-8",
        )
        before = cache.read_bytes()
        with self.assertRaisesRegex(ValueError, "stale CMake cache"):
            inspect_project_setup(self.cmake, ninja, self.project, build, self.env,
                                  "linux-clang-development", compiler=str(compiler),
                                  sdk_prefix=str(prefix))
        self.assertEqual(cache.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
