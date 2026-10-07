"""Explicitly opted-in editor setup, separate from the default engine tooling."""
from __future__ import annotations

import platform
import shutil
from pathlib import Path

import cmake_targets


SUPPORTED_PRESETS = tuple(f"{family}-clang-{flavor}" for family in ("linux", "macos")
                          for flavor in ("debug", "development", "asan-ubsan"))
QT_PACKAGES = ("qt6-base-dev", "qt6-wayland")


def supported_host() -> bool:
    machine = platform.machine().lower()
    return ((platform.system() == "Linux" and machine in ("x86_64", "amd64"))
            or (platform.system() == "Darwin" and machine in ("arm64", "aarch64", "x86_64", "amd64")))


def validate_editor_options(args, engine) -> None:
    if not args.with_editor:
        return
    if not supported_host():
        raise engine.EngineError("The optional Ludus editor supports Linux x64 and macOS arm64/x64")
    family = "macos-" if platform.system() == "Darwin" else "linux-"
    if args.preset not in SUPPORTED_PRESETS or not args.preset.startswith(family):
        raise engine.EngineError("Editor setup requires " + ", ".join(SUPPORTED_PRESETS) + "; browser editor setup is unsupported")


def setup_editor(args, engine) -> None:
    if not args.with_editor:
        return
    validate_editor_options(args, engine)
    root = engine.repo_root()
    if platform.system() == "Darwin":
        brew = shutil.which("brew")
        if not args.no_system_install:
            if not brew:
                raise engine.EngineError("Install Qt 6.4+ manually, or install Homebrew, then rerun editor setup")
            engine.run([brew, "install", "qtbase"], cwd=root)
        else:
            print("Using existing Qt 6.4+ with the Cocoa plugin (--no-system-install).")
        # Homebrew Qt is optional and may be keg-only. Keep its machine path in
        # the local CMake cache, never in committed presets or the engine SDK.
        qt_args = []
        if brew:
            status, output = engine.capture_command_quiet([brew, "--prefix", "qtbase"], cwd=root)
            if status == 0 and (Path(output.strip()) / "lib/cmake/Qt6").is_dir():
                qt_args = [f"-DQt6_DIR={output.strip()}/lib/cmake/Qt6"]
        cmake_targets.query_codemodel(engine.build_dir_for_preset(root, args.preset), "ludus-editor")
        engine.cmake_configure(root, args.preset, ["-DLUDUS_BUILD_EDITOR=ON", *qt_args])
        engine.cmake_build(root, args.preset, ["--target", "ludus_editor"])
        return
    if not args.no_system_install:
        if not engine.host_supports_apt_install():
            raise engine.EngineError("Automatic editor prerequisites require Ubuntu/Debian; install Qt 6.4+ manually and use --no-system-install")
        missing = []
        for package in QT_PACKAGES:
            status, output = engine.capture_command_quiet(
                ["dpkg-query", "--show", "--showformat=${Status}", package], cwd=root)
            if status != 0 or output.strip() != "install ok installed":
                missing.append(package)
        if missing:
            prefix = engine.sudo_command_prefix()
            print("Installing optional editor Qt packages. This may ask for your sudo password.")
            engine.run([*prefix, "apt-get", "update"], cwd=root)
            engine.run([*prefix, "apt-get", "install", "-y", *missing], cwd=root)
    else:
        print("Using existing Qt 6.4+ packages for the optional editor (--no-system-install).")

    cmake_targets.query_codemodel(engine.build_dir_for_preset(root, args.preset), "ludus-editor")
    engine.cmake_configure(root, args.preset, ["-DLUDUS_BUILD_EDITOR=ON"])
    engine.cmake_build(root, args.preset, ["--target", "ludus_editor"])
