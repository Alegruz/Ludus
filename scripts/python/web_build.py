"""Separate, pinned browser dependency path; never installs native Conan packages."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import runpy
import shlex
import shutil
import subprocess
import urllib.request
import venv

from formatting import format_source

PRESETS = ("web-emscripten-development", "web-emscripten-release")


def command(args, engine) -> int:
    # Native workflows never import this module. Reuse the calling engine module
    # so errors retain its identity when the CLI runs as __main__.
    EngineError = engine.EngineError
    repo_root = engine.repo_root
    run = engine.run
    run_format_check = engine.run_format_check
    run_foundational_includes = engine.run_foundational_includes

    root = repo_root()
    if args.preset not in PRESETS:
        raise EngineError("Unknown browser preset: " + args.preset)
    if args.command not in ("init", "bootstrap", "doctor", "build", "test", "check"):
        raise EngineError("Browser SDK installation/profiling is not supported by this stage")
    if args.command == "init" and (args.all_presets or args.validate or args.ci):
        raise EngineError("Use web init --preset-only, then web build/test/check separately")
    lock = json.loads((root / "config/web_toolchain.json").read_text())
    sdk = root / "out/host-tools/emsdk"
    host_bin = root / "out/host-tools/venv/bin"

    try:
        if args.command in ("init", "bootstrap"):
            native = json.loads((root / "config/tool_versions.json").read_text())["managed"]
            if not (host_bin / "python").exists():
                venv.EnvBuilder(with_pip=True).create(host_bin.parent)
            run([host_bin / "python", "-m", "pip", "install",
                 f"cmake=={native['cmake']}", f"ninja=={native['ninja']}"], cwd=root)
            if not (sdk / "emsdk").exists():
                if sdk.exists() and any(sdk.iterdir()):
                    raise EngineError("Incomplete SDK directory; preserve it and repair before retrying")
                archive = root / "out/host-tools/emsdk.tar.gz"
                urllib.request.urlretrieve(lock["emsdk_archive"], archive)
                if hashlib.sha256(archive.read_bytes()).hexdigest() != lock["emsdk_archive_sha256"]:
                    raise EngineError("SDK source archive checksum mismatch; extraction refused")
                sdk.mkdir(parents=True, exist_ok=True)
                run(["tar", "-xzf", archive, "--strip-components=1", "-C", sdk], cwd=root)
            run([sdk / "emsdk", "install", lock["emscripten"]], cwd=root)
            run([sdk / "emsdk", "activate", lock["emscripten"]], cwd=root)

        # Reuse W0's SDK version and vendored-port integrity validation.
        probe_tools = runpy.run_path(str(root / "scripts/webgpu-probe"))
        emscripten, env = probe_tools["verify_sdk"](sdk, lock)
        for name in ("cmake", "ninja"):
            if not (host_bin / name).is_file():
                raise EngineError("Missing pinned host tool; run ./init.sh " + args.preset + " --preset-only")
        native = json.loads((root / "config/tool_versions.json").read_text())["managed"]
        for name in ("cmake", "ninja"):
            actual = subprocess.check_output([host_bin / name, "--version"], text=True)
            expected = native[name] if name == "cmake" else native[name].rsplit(".", 1)[0]
            if expected not in actual.splitlines()[0]:
                raise EngineError(f"Pinned host {name} differs: {actual.splitlines()[0]}")
        if args.command == "doctor":
            print("Browser tools verified; no Conan, Volk, Wayland, or native helper dependency.")
            return 0

        cmake = host_bin / "cmake"
        run([emscripten / "emcmake", cmake, "--preset", args.preset], cwd=root, env=env)
        if args.command in ("init", "bootstrap"):
            print("Browser configure complete; next: ./scripts/build " + args.preset)
            return 0
        build = root / "out/build" / args.preset
        if args.command in ("build", "test"):
            extra = getattr(args, "extra", [])
            run([cmake, "--build", "--preset", args.preset, *extra], cwd=root, env=env)
            if args.command == "test":
                options = ["-L", args.label] if args.label else []
                run([host_bin / "ctest", "--preset", args.preset, *options], cwd=root, env=env)
            return 0

        all_checks = args.all or not (args.format or args.tidy or args.include_cleaner)
        if args.include_cleaner:
            raise EngineError("Browser include-cleaner is not implemented; use format/tidy checks")
        if args.format or all_checks:
            run_format_check(root, fix=args.fix)
            run_foundational_includes(root)
            for probe in ("web-foundation-probe", "web-observability-probe"):
                path = root / "tools" / probe / "main.cpp"
                original = path.read_text()
                formatted = format_source(original, path, "clang-format-18")
                if args.fix:
                    path.write_text(formatted)
                elif formatted != original:
                    raise EngineError(f"Browser {probe} needs repository formatting")
        if args.tidy or all_checks:
            tidy = shutil.which("clang-tidy-18")
            if not tidy:
                raise EngineError("clang-tidy 18 is required")
            entries = json.loads((build / "compile_commands.json").read_text())
            sysroot = emscripten / "cache/sysroot"
            for entry in entries:
                command_line = shlex.split(entry["command"])
                flags = []
                index = 1
                while index < len(command_line):
                    flag = command_line[index]
                    if flag == "-o":
                        index += 2
                        continue
                    if flag not in ("-c", entry["file"]):
                        flags.append(flag)
                    index += 1
                flags += ["--target=wasm32-unknown-emscripten", f"--sysroot={sysroot}", "-DEMSCRIPTEN",
                          "-isystem", str(sysroot / "include/c++/v1"),
                          "-isystem", str(sysroot / "include/compat")]
                run([tidy, "--warnings-as-errors=*", entry["file"], "--", *flags], cwd=root, env=env)
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise EngineError(str(error)) from error
