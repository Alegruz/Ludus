"""Pinned Catch2 with Ludus's lightweight test diagnostic boundary.

Thanks to Catch2 Authors, Catch2 3.4.0, https://github.com/catchorg/Catch2/tree/v3.4.0.
The runtime is rebuilt with the same headers used by tests; never mix modified
inline templates with the upstream binary package. Runtime includes remain
private to the dependency build, outside the engine's compilation graph.
"""
import os
import runpy
from pathlib import Path

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get


class Catch2PinnedRecipe(ConanFile):
    name = "catch2"
    version = "3.4.0"
    package_type = "static-library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"fPIC": [True, False]}
    default_options = {"fPIC": True}

    def export_sources(self):
        support = Path(self.recipe_folder).parents[1] / "tests" / "support"
        for name in ("prepare_catch_headers.py", "catch_stream.hpp", "catch_stream.cpp"):
            copy(self, name, src=str(support), dst=self.export_sources_folder)

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def layout(self):
        cmake_layout(self)

    def source(self):
        get(self, "https://codeload.github.com/catchorg/Catch2/tar.gz/refs/tags/v3.4.0",
            sha256="122928b814b75717316c71af69bd2b43387643ba076a6ec16e7882bfb2dfacbb",
            strip_root=True, filename="catch2-3.4.0.tar.gz")
        source = Path(self.source_folder)
        helpers = runpy.run_path(str(Path(self.export_sources_folder) / "prepare_catch_headers.py"))
        helpers["prepare"](source / "src" / "catch2", source / "src")
        for name in ("catch_stream.hpp", "catch_stream.cpp"):
            copy(self, name, src=self.export_sources_folder, dst=str(source / "src" / "catch2" / "internal"))
        # Upstream .cpp files previously relied on the heavy transitive headers.
        # Keep their includes private while test consumers parse the lean API.
        (source / "src" / "catch2" / "internal" / "ludus_runtime_includes.hpp").write_text(
            "#include <ostream>\n#include <chrono>\n", encoding="utf-8")
        with (source / "CMakeLists.txt").open("a", encoding="utf-8") as cmake:
            cmake.write('''
 target_sources(Catch2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/catch2/internal/catch_stream.cpp)
 if(MSVC)
     target_compile_options(Catch2 PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/src/catch2/internal/ludus_runtime_includes.hpp)
 else()
     target_compile_options(Catch2 PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/src/catch2/internal/ludus_runtime_includes.hpp)
 endif()
 install(FILES src/catch2/internal/catch_stream.hpp DESTINATION include/catch2/internal)
 install(FILES src/catch2/catch_chrono_stringmaker.hpp DESTINATION include/catch2)
''')

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["BUILD_SHARED_LIBS"] = False
        toolchain.variables["CATCH_BUILD_TESTING"] = False
        toolchain.variables["CATCH_INSTALL_DOCS"] = False
        toolchain.variables["CATCH_INSTALL_EXTRAS"] = False
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        CMake(self).install()
        copy(self, "LICENSE.txt", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "Catch2")
        core = self.cpp_info.components["catch2"]
        core.set_property("cmake_target_name", "Catch2::Catch2")
        core.libs = ["Catch2d" if self.settings.build_type == "Debug" else "Catch2"]
        if self.settings.os in ("Linux", "FreeBSD"):
            core.system_libs = ["pthread"]
        main = self.cpp_info.components["catch2main"]
        main.set_property("cmake_target_name", "Catch2::Catch2WithMain")
        main.libs = ["Catch2Maind" if self.settings.build_type == "Debug" else "Catch2Main"]
        main.requires = ["catch2"]
