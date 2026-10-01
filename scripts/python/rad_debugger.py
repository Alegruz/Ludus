"""Optional Linux RAD tooling; engine runtime and normal builds do not depend on it."""
from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import shutil
from contextlib import ExitStack
from pathlib import Path
from typing import Any


DEBUG_PRESETS = ("linux-clang-debug", "linux-clang-development")
RAD_PACKAGES = (
    "pkg-config", "libfreetype6-dev", "libx11-dev", "libxext-dev",
    "libxfixes-dev", "libgl-dev", "libegl-dev",
)


def read_json(path: Path, engine: Any) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise engine.EngineError(f"cannot read {path}: {exc}") from exc


def pin(root: Path, engine: Any) -> dict[str, str]:
    data = read_json(root / "config" / "rad_debugger.json", engine)
    if (not isinstance(data, dict)
            or not isinstance(data.get("version"), str)
            or data.get("repository") != "https://github.com/EpicGames/raddebugger.git"
            or not re.fullmatch(r"[0-9a-f]{40}", str(data.get("revision", "")))):
        raise engine.EngineError("invalid config/rad_debugger.json")
    return data


def install_dir(root: Path, data: dict[str, str]) -> Path:
    return root / "out" / "host-tools" / "raddebugger" / data["revision"]


def executable(path: Path) -> bool:
    return path.is_file() and os.access(path, os.X_OK)


def managed_directory(root: Path, binary: Path) -> Path | None:
    base = (root / "out/host-tools/raddebugger").resolve()
    try:
        relative = binary.resolve().relative_to(base)
    except ValueError:
        return None
    if (len(relative.parts) == 4 and re.fullmatch(r"[0-9a-f]{40}", relative.parts[0])
            and relative.parts[1:] == ("source", "build", "raddbg")):
        return base / relative.parts[0]
    return None


def debugger_identity(root: Path, binary: Path) -> str:
    directory = managed_directory(root, binary)
    if directory is not None:
        return "revision-" + directory.name
    # External installations may be replaced at the same path. Isolate their
    # autosaved configuration by executable contents, without opening the GUI.
    digest = hashlib.sha256()
    with binary.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return "external-" + digest.hexdigest()


def managed_binary(root: Path, engine: Any) -> Path | None:
    data = pin(root, engine)
    directory = install_dir(root, data)
    binary = directory / "source" / "build" / "raddbg"
    stamp = directory / "build.json"
    if executable(binary) and stamp.is_file():
        try:
            if read_json(stamp, engine).get("pin") == data:
                return binary
        except (engine.EngineError, AttributeError):
            pass
    return None


def resolve_binary(root: Path, engine: Any, override: str | None = None) -> Path:
    preference = override if override is not None else os.environ.get("LUDUS_RAD_DEBUGGER")
    if preference is not None:
        candidate = Path(preference).expanduser()
        if not candidate.is_absolute():
            candidate = Path(shutil.which(preference) or candidate)
        if executable(candidate):
            return candidate.resolve()
        raise engine.EngineError(f"RAD executable preference is invalid: {preference!r}")
    binary = managed_binary(root, engine)
    if binary is not None:
        return binary
    found = shutil.which("raddbg")
    if found and executable(Path(found)):
        return Path(found).resolve()
    raise engine.EngineError(
        f"Pinned RAD {pin(root, engine)['version']} is not installed or its build is incomplete. "
        "RAD is optional. Run ./scripts/setup-rad-debugger "
        "or set LUDUS_RAD_DEBUGGER to an existing executable."
    )


def tool_status(root: Path, engine: Any) -> Any:
    try:
        binary = resolve_binary(root, engine)
        managed = binary == managed_binary(root, engine)
        version = pin(root, engine)["version"] if managed else "external (unverified)"
        return engine.ToolStatus("RAD Debugger", False, True, version, str(binary), "optional Linux x64", True)
    except engine.EngineError as exc:
        return engine.ToolStatus("RAD Debugger", False, False, "", "", str(exc), False)


def require_host(engine: Any) -> None:
    if platform.system() != "Linux" or platform.machine().lower() not in ("x86_64", "amd64"):
        raise engine.EngineError("the RAD integration currently supports Linux x64 only")


def build_tools(root: Path, engine: Any) -> tuple[str | None, str | None, bool]:
    major = engine.llvm_major_version(engine.load_tool_versions(root))
    compiler = shutil.which(f"clang-{major}")
    archiver = shutil.which(f"llvm-ar-{major}")
    libraries_ok = False
    if shutil.which("pkg-config"):
        code, _ = engine.capture_command_quiet(
            ["pkg-config", "--exists", "freetype2", "x11", "xext", "xfixes", "gl", "egl"], cwd=root
        )
        libraries_ok = code == 0
    return compiler, archiver, libraries_ok


def setup(args: Any, engine: Any) -> int:
    root = engine.repo_root()
    require_host(engine)
    data = pin(root, engine)
    directory = install_dir(root, data)
    directory.mkdir(parents=True, exist_ok=True)
    # Serialize setup so concurrent requests cannot mutate the same checkout.
    import fcntl

    with (directory / "setup.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise engine.EngineError("RAD is in use or another setup is running") from exc
        return setup_locked(root, directory, data, args, engine)


def setup_locked(root: Path, directory: Path, data: dict[str, str], args: Any, engine: Any) -> int:
    compiler, archiver, libraries_ok = build_tools(root, engine)
    major = engine.llvm_major_version(engine.load_tool_versions(root))
    if not compiler or not archiver or not libraries_ok or not shutil.which("git"):
        packages = ["git", f"clang-{major}", f"llvm-{major}", *RAD_PACKAGES]
        remedy = "sudo apt-get install -y " + " ".join(packages)
        if args.no_system_install or not engine.host_supports_apt_install():
            raise engine.EngineError("missing RAD build prerequisites; install them first: " + remedy)
        prefix = engine.sudo_command_prefix()
        engine.run([*prefix, "apt-get", "update"], cwd=root)
        engine.run([*prefix, "apt-get", "install", "-y", *packages], cwd=root)
        compiler, archiver, libraries_ok = build_tools(root, engine)
    if not compiler or not archiver or not libraries_ok or not shutil.which("git"):
        raise engine.EngineError("RAD build prerequisites are still unavailable")

    source = directory / "source"
    source.mkdir(exist_ok=True)
    if not (source / ".git").is_dir():
        if any(source.iterdir()):
            raise engine.EngineError(f"refusing to replace unexpected files in {source}")
        engine.run(["git", "init", source], cwd=root)
    code, origin = engine.capture_command_quiet(["git", "remote", "get-url", "origin"], cwd=source)
    if code != 0:
        engine.run(["git", "remote", "add", "origin", data["repository"]], cwd=source)
    elif origin.strip() != data["repository"]:
        raise engine.EngineError(f"unexpected RAD source origin in {source}")
    code, head = engine.capture_command_quiet(["git", "rev-parse", "HEAD"], cwd=source)
    if code == 0 and head.strip() != data["revision"]:
        raise engine.EngineError(f"unexpected RAD source revision in {source}; preserve it and move it aside")
    if code != 0:
        engine.run(["git", "fetch", "--depth=1", "origin", data["revision"]], cwd=source)
        engine.run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=source)
    _, head = engine.capture_command_quiet(["git", "rev-parse", "HEAD"], cwd=source)
    code, changes = engine.capture_command_quiet(["git", "status", "--porcelain"], cwd=source)
    if head.strip() != data["revision"] or code != 0 or changes.strip():
        raise engine.EngineError(f"RAD source is not a clean pinned checkout: {source}")
    stamp_data = {"pin": data, "compiler": compiler, "archiver": archiver}
    stamp = directory / "build.json"
    if managed_binary(root, engine) and read_json(stamp, engine) == stamp_data:
        print(f"RAD {data['version']} is already installed: {managed_binary(root, engine)}")
        return 0
    stamp.unlink(missing_ok=True)
    env = os.environ.copy()
    env.update(CC=compiler, AR=archiver)
    engine.run(["bash", "build.sh", "raddbg", "release"], cwd=source, env=env)
    binary = source / "build" / "raddbg"
    if not executable(binary):
        raise engine.EngineError(f"RAD build did not produce {binary}")
    stamp.write_text(json.dumps(stamp_data, indent=2) + "\n", encoding="utf-8")
    print(f"Installed optional RAD {data['version']}: {binary}")
    return 0


def query_codemodel(build_dir: Path) -> None:
    query = build_dir / ".cmake" / "api" / "v1" / "query" / "client-ludus-debug"
    query.mkdir(parents=True, exist_ok=True)
    (query / "codemodel-v2").touch()


def target_executable(build_dir: Path, target: str, engine: Any) -> Path:
    reply = build_dir / ".cmake" / "api" / "v1" / "reply"
    indexes = sorted(reply.glob("index-*.json"))
    if not indexes:
        raise engine.EngineError("no CMake File API reply; run scripts/debug without --no-build first")
    try:
        index = read_json(indexes[-1], engine)
        reference = index["reply"]["client-ludus-debug"]["codemodel-v2"]["jsonFile"]
        model = read_json(reply / reference, engine)
        matches = [entry for config in model["configurations"] for entry in config["targets"]
                   if entry["name"] == target]
        if len(matches) != 1:
            raise engine.EngineError(f"CMake target {target!r} does not resolve to one target in {build_dir}")
        detail = read_json(reply / matches[0]["jsonFile"], engine)
        if detail["type"] != "EXECUTABLE" or len(detail.get("artifacts", [])) != 1:
            raise engine.EngineError(f"CMake target {target!r} is not an executable")
        return (build_dir / detail["artifacts"][0]["path"]).resolve()
    except (KeyError, TypeError) as exc:
        raise engine.EngineError("invalid CMake File API reply; reconfigure without --no-build") from exc


def validate_arguments(arguments: list[str], engine: Any) -> None:
    # RAD's rd_init quotes space-containing tokens, but its Linux launch parser
    # passes those quotes to execve unchanged. Empty/quoted tokens also change.
    for argument in arguments:
        if not argument or '"' in argument or any(char.isspace() or ord(char) < 32 for char in argument):
            raise engine.EngineError(
                "RAD 0.9.29 Linux cannot preserve empty, quoted, or whitespace-containing launch arguments; "
                "use simple arguments or another debugger until upstream fixes its argv parser"
            )


def launch(args: Any, engine: Any) -> int:
    require_host(engine)
    if args.preset not in DEBUG_PRESETS:
        raise engine.EngineError("RAD supports native stepping presets only: " + ", ".join(DEBUG_PRESETS))
    if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.+-]*", args.target):
        raise engine.EngineError("invalid CMake executable target name")
    validate_arguments(args.arguments, engine)
    root = engine.repo_root()
    binary = resolve_binary(root, engine, args.debugger)
    cwd = Path(args.cwd).expanduser().resolve() if args.cwd else root
    if not cwd.is_dir():
        raise engine.EngineError(f"debug working directory does not exist: {cwd}")
    if not args.dry_run and not os.environ.get("DISPLAY"):
        raise engine.EngineError("RAD needs an X11 display (XWayland on Wayland); use --dry-run on headless hosts")
    target_session = root / "out" / "debug" / "rad" / args.preset / args.target
    target_session.mkdir(parents=True, exist_ok=True)
    import fcntl

    with (target_session / "session.lock").open("a") as lock, ExitStack() as resources:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise engine.EngineError(f"a RAD session for {args.target} is already open") from exc
        directory = managed_directory(root, binary)
        installed_pin = None
        if directory is not None:
            setup_lock = resources.enter_context((directory / "setup.lock").open("a"))
            try:
                fcntl.flock(setup_lock, fcntl.LOCK_SH | fcntl.LOCK_NB)
            except BlockingIOError as exc:
                raise engine.EngineError("RAD setup is running; retry the launch when it completes") from exc
            stamp = read_json(directory / "build.json", engine)
            installed_pin = stamp.get("pin") if isinstance(stamp, dict) else None
            if (not isinstance(installed_pin, dict)
                    or installed_pin.get("revision") != directory.name
                    or installed_pin.get("repository") != "https://github.com/EpicGames/raddebugger.git"
                    or not isinstance(installed_pin.get("version"), str)):
                raise engine.EngineError("managed RAD build is incomplete; rerun scripts/setup-rad-debugger")
        build_dir = engine.build_dir_for_preset(root, args.preset)
        if not args.no_build:
            engine.ensure_bootstrap_for_preset(root, args.preset)
            query_codemodel(build_dir)
            engine.cmake_configure(root, args.preset)
        exe = target_executable(build_dir, args.target, engine)
        if not args.no_build:
            engine.cmake_build(root, args.preset, ["--target", args.target])
        if not executable(exe):
            raise engine.EngineError(f"debug target is not built or executable: {exe}")
        identity = debugger_identity(root, binary)
        session = target_session / "versions" / identity
        session.mkdir(parents=True, exist_ok=True)
        # RAD's rd_init treats absolute Linux paths as flags. A ./ relative path
        # is unambiguously an executable, including names starting with a hyphen.
        relative_exe = "./" + os.path.relpath(exe, cwd)
        user = session / "session.raddbg_user"
        project = session / "session.raddbg_project"
        for path, kind in ((user, "user"), (project, "project")):
            if not path.exists():
                path.write_text(f"// raddbg 0.9.29 {kind}\n", encoding="utf-8")
        description = {"preset": args.preset, "target": args.target, "executable": str(exe),
                       "arguments": args.arguments, "cwd": str(cwd), "debugger": str(binary),
                       "debugger_identity": identity, "managed_pin": installed_pin}
        (session / "launch.json").write_text(json.dumps(description, indent=2) + "\n", encoding="utf-8")
        command = [binary, f"--user:{user}", f"--project:{project}", f"--logs:{session / 'logs'}",
                   "--", relative_exe, *args.arguments]
        print(f"RAD target: {exe}\nWorking directory: {cwd}\nSession: {session}")
        print("Set breakpoints and run the temporary command-line target in RAD; do not Save To Project.")
        if args.dry_run:
            print(engine.command_line(command))
            return 0
        engine.run(command, cwd=cwd)
    return 0
