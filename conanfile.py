from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class LudusRecipe(ConanFile):
    name = "ludus"
    version = "0.1.0"
    package_type = "static-library"
    settings = "os", "arch", "compiler", "build_type"

    # Text/font rendering pins (.kiro/specs/text-font-rendering/design.md section 2).
    # FreeType 2.14.3 is on ConanCenter; HarfBuzz 14.5.1 is not, so it is built
    # from a local recipe under third_party/harfbuzz (exported during bootstrap).
    # FreeType is configured static with PNG/zlib/Brotli/BZip2 and its own
    # HarfBuzz-assisted auto-hinter disabled; HarfBuzz is static with OpenType +
    # FreeType integration and no GLib/ICU/Graphite/Cairo/subset/utils/tests or
    # experimental raster/vector/GPU/wasm backends.
    def requirements(self) -> None:
        if self.settings.os != "Macos":
            self.requires("volk/1.4.357.0")
        self.requires("harfbuzz/14.5.1")

    def configure(self) -> None:
        self.options["freetype"].shared = False
        self.options["freetype"].with_png = False
        self.options["freetype"].with_zlib = False
        self.options["freetype"].with_brotli = False
        self.options["freetype"].with_bzip2 = False

    def build_requirements(self) -> None:
        if self.conf.get("user.ludus:build_tests", default=True, check_type=bool):
            self.test_requires("catch2/3.4.0")

    def generate(self) -> None:
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = None
        toolchain.generate()
