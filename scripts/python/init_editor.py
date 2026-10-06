"""Explicitly opted-in editor setup, separate from the default engine tooling."""
from __future__ import annotations

import platform

import cmake_targets


SUPPORTED_PRESETS = ("linux-clang-debug", "linux-clang-development")
QT_PACKAGES = ("qt6-base-dev", "qt6-wayland")


def validate_editor_options(args, engine) -> None:
    if not args.with_editor:
        return
    if platform.system() != "Linux" or platform.machine().lower() not in ("x86_64", "amd64"):
        raise engine.EngineError("The optional Ludus editor currently supports Linux x64 only")
    if args.preset not in SUPPORTED_PRESETS:
        raise engine.EngineError("Editor setup requires linux-clang-debug or linux-clang-development; browser editor setup is unsupported")


def setup_editor(args, engine) -> None:
    if not args.with_editor:
        return
    validate_editor_options(args, engine)
    root = engine.repo_root()
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
