from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class LudusRecipe(ConanFile):
    name = "ludus"
    version = "0.1.0"
    package_type = "static-library"
    settings = "os", "arch", "compiler", "build_type"

    def requirements(self) -> None:
        self.requires("volk/1.3.296.0")

    def build_requirements(self) -> None:
        self.test_requires("catch2/3.4.0")

    def generate(self) -> None:
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = None
        toolchain.generate()
