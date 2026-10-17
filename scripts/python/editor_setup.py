"""Select a prepared engine preset for clangd and VS Code tasks without editing user settings."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

import engine
from analysis_inventory import inventory


def validate(root: Path, preset: str) -> Path:
    if preset not in engine.HOST_PRESETS:
        raise engine.EngineError(f"Choose a native preset for this host: {', '.join(engine.HOST_PRESETS)}")
    engine.ensure_bootstrap_for_preset(root, preset)
    build = engine.build_dir_for_preset(root, preset)
    cache = build / "CMakeCache.txt"
    home = engine.cmake_cache_value(cache, "CMAKE_HOME_DIRECTORY")
    if not home or Path(home).resolve() != root.resolve():
        raise engine.EngineError(f"Missing or moved configure cache; run ./scripts/build {preset}")
    reasons = engine.cmake_cache_repair_reasons(root, preset)
    if reasons:
        raise engine.EngineError(f"Stale {preset}: {'; '.join(reasons)}; run ./scripts/build {preset}")
    for tool in (engine.clang_cxx(root), engine.ninja(root), engine.cmake(root)):
        if not tool.is_file() or not os.access(tool, os.X_OK):
            raise engine.EngineError(f"Missing tool {tool}; run ./init.sh")
    llvm_major = engine.pinned_llvm_major(root)
    compiler_version = engine.executable_version(str(engine.clang_cxx(root)), ("--version",), root)
    if not engine.version_tuple(compiler_version) or engine.version_tuple(compiler_version)[0] != llvm_major:
        raise engine.EngineError(f"Editor requires reference Clang {llvm_major}")
    clangd = engine.host_tools_bin_dir(root) / "clangd"
    if not clangd.is_file() or not os.access(clangd, os.X_OK):
        raise engine.EngineError(f"Missing clangd-{llvm_major}; install it and rerun ./init.sh")
    result = subprocess.run([str(clangd), "--version"], text=True, capture_output=True, timeout=15)
    if result.returncode or not re.search(rf"\bclangd version {llvm_major}\.", result.stdout):
        raise engine.EngineError(f"Editor requires reference clangd {llvm_major}")
    # CMake owns inheritance and host conditions. JSON parsing alone cannot
    # establish whether a preset is actually selectable.
    for kind in ("configure", "build", "test"):
        result = subprocess.run([str(engine.cmake(root)), f"--list-presets={kind}"],
                                cwd=root, env=engine.tool_env(root), text=True, capture_output=True, timeout=30)
        if result.returncode or not re.search(rf'^\s*"{re.escape(preset)}"(?:\s|$)', result.stdout, re.MULTILINE):
            raise engine.EngineError(f"{kind} preset {preset} unavailable: {result.stderr.strip()}")
    inventory(root, build)
    return clangd


def selection_path(root: Path) -> Path:
    return root / "out/editor/selection.json"


def select(root: Path, preset: str) -> None:
    validate(root, preset)
    path = selection_path(root)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps({"version": 1, "root": str(root.resolve()), "preset": preset}, indent=2) + "\n")
    temporary.replace(path)
    print(f"Editor preset: {preset}. Restart clangd in VS Code to apply the selection.")


def selected(root: Path) -> tuple[str, Path]:
    try:
        state = json.loads(selection_path(root).read_text())
    except (OSError, ValueError) as error:
        raise engine.EngineError("Editor setup missing; run ./scripts/setup-editor <prepared-native-preset>") from error
    if not isinstance(state, dict) or state.get("version") != 1 or state.get("root") != str(root.resolve()):
        raise engine.EngineError("Editor setup moved or invalid; rerun ./scripts/setup-editor <prepared-native-preset>")
    preset = state.get("preset")
    if not isinstance(preset, str):
        raise engine.EngineError("Invalid editor preset")
    return preset, validate(root, preset)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("select", "clangd", "task"))
    parser.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    root = engine.repo_root()
    try:
        if args.action == "select":
            if len(args.arguments) != 1:
                raise engine.EngineError("Usage: ./scripts/setup-editor <prepared-native-preset>")
            select(root, args.arguments[0])
        else:
            preset, clangd = selected(root)
            if args.action == "clangd":
                # Thanks to LLVM's "Compile commands" documentation: use the
                # selected database. Clangd 18 and the reference Clang driver
                # share headers; macOS presets already supply SDK/libc++ paths.
                # Query-driver adds explicit SDK -isystem paths ahead of Clang's
                # builtin C headers and breaks ptrdiff_t/CHAR_BIT on macOS 26.5.
                # No external driver execution is needed for this LLVM-only lane.
                # https://clangd.llvm.org/design/compile-commands
                forbidden = ("--compile-commands-dir", "-compile-commands-dir", "--query-driver", "-query-driver")
                if any(arg.startswith(forbidden) for arg in args.arguments):
                    raise engine.EngineError("Preset owns clangd database and driver arguments")
                argv = [str(clangd), f"--compile-commands-dir={engine.build_dir_for_preset(root, preset)}",
                        *args.arguments]
                os.execv(str(clangd), argv)
            else:
                if len(args.arguments) != 1:
                    raise engine.EngineError("Expected one editor task")
                commands = {"build": ["build", preset], "test": ["test", preset],
                            "format": ["check", preset, "--format"], "tidy": ["check", preset, "--tidy"],
                            "format-fix": ["check", preset, "--format", "--fix"],
                            "include-cleaner": ["check", preset, "--include-cleaner"],
                            "analyzer": ["check", preset, "--analyzer"], "tooling": ["check", "--tooling"],
                            "profile-build": ["profile-build", preset], "build-budget": ["check-build-budget", preset]}
                command = commands.get(args.arguments[0])
                if command is None:
                    raise engine.EngineError("Unknown editor task")
                return engine.main(command)
        return 0
    except (OSError, ValueError, subprocess.SubprocessError, engine.EngineError) as error:
        print(f"Editor setup: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
