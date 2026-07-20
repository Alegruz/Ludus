#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import venv
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


DEFAULT_PRESET = "linux-clang-development"
PROJECT_CXX_STANDARD = "23"
BOOTSTRAP_STATE_VERSION = 1

PRESET_BUILD_TYPES: dict[str, str] = {
    "linux-clang-debug": "Debug",
    "linux-clang-development": "RelWithDebInfo",
    "linux-clang-asan-ubsan": "RelWithDebInfo",
    "linux-clang-release": "Release",
}

FORMAT_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
TIDY_ROOTS = ("modules", "apps")

BOOTSTRAP_INPUTS = (
    "config/tool_versions.json",
    "config/conan/profiles/linux-clang-x86_64",
    "conanfile.py",
    "conan.lock",
    "CMakePresets.json",
)


class EngineError(RuntimeError):
    """Exception raised for engine-related errors."""


@dataclass(frozen=True)
class ToolStatus:
    """Represents the status of a development tool."""
    name: str
    required: bool
    found: bool
    version: str
    path: str
    requirement: str
    ok: bool


@dataclass(frozen=True)
class AptPackageGroup:
    """Represents one prerequisite that can be satisfied by one of several apt packages."""
    purpose: str
    candidates: tuple[str, ...]


def find_repo_root(start: Path) -> Path:
    """Find the repository root by searching for tool_versions.json in the config directory."""
    current = start.resolve()
    for candidate in (current, *current.parents):
        if (candidate / "config" / "tool_versions.json").is_file():
            return candidate
    raise EngineError("could not locate repository root from " + str(start))


def repo_root() -> Path:
    """Get the repository root from the current module's location."""
    return find_repo_root(Path(__file__).resolve())


def load_tool_versions(root: Path) -> dict[str, dict[str, str]]:
    """Load tool versions configuration from tool_versions.json."""
    versions_path = root / "config" / "tool_versions.json"
    try:
        data = json.loads(versions_path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise EngineError(f"failed to read {versions_path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise EngineError(f"failed to parse {versions_path}: {exc}") from exc

    if not isinstance(data.get("managed"), dict) or not isinstance(data.get("minimum"), dict):
        raise EngineError(f"{versions_path} must contain 'managed' and 'minimum' objects")
    return data


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def bootstrap_fingerprint(root: Path) -> dict[str, object]:
    files: dict[str, str] = {}
    for relative_path in BOOTSTRAP_INPUTS:
        path = root / relative_path
        files[relative_path] = file_sha256(path) if path.is_file() else "missing"
    return {
        "version": BOOTSTRAP_STATE_VERSION,
        "cxx_standard": PROJECT_CXX_STANDARD,
        "files": files,
    }


def bootstrap_marker_path(root: Path, preset: str) -> Path:
    return root / "out" / "conan" / preset / ".ludus-bootstrap.json"


def write_bootstrap_marker(root: Path, preset: str) -> None:
    marker = bootstrap_marker_path(root, preset)
    marker.write_text(
        json.dumps(bootstrap_fingerprint(root), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def bootstrap_marker_is_current(root: Path, preset: str) -> bool:
    marker = bootstrap_marker_path(root, preset)
    if not marker.is_file():
        return False
    try:
        actual = json.loads(marker.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    return actual == bootstrap_fingerprint(root)


def version_tuple(version: str) -> tuple[int, ...]:
    """Extract version numbers from a version string."""
    match = re.search(r"(\d+(?:\.\d+)*)", version)
    if match is None:
        return ()
    return tuple(int(part) for part in match.group(1).split("."))


def version_at_least(actual: str, minimum: str) -> bool:
    """Check if actual version is at least the minimum version."""
    actual_parts = version_tuple(actual)
    minimum_parts = version_tuple(minimum)
    if not actual_parts:
        return False

    width = max(len(actual_parts), len(minimum_parts))
    return actual_parts + (0,) * (width - len(actual_parts)) >= minimum_parts + (0,) * (width - len(minimum_parts))


def command_line(argv: Sequence[object]) -> str:
    """Format command arguments as a quoted command line string."""
    return " ".join(shlex_quote(str(arg)) for arg in argv)


def shlex_quote(value: str) -> str:
    """Quote a string for safe use in shell commands."""
    if re.fullmatch(r"[A-Za-z0-9_@%+=:,./-]+", value):
        return value
    return "'" + value.replace("'", "'\"'\"'") + "'"


def run(
    argv: Sequence[object],
    *,
    cwd: Path,
    env: dict[str, str] | None = None,
    capture: bool = False,
) -> subprocess.CompletedProcess[str]:
    """Run a command and return the result."""
    print(f"+ {command_line(argv)}", flush=True)
    try:
        return subprocess.run(
            [str(arg) for arg in argv],
            cwd=str(cwd),
            env=env,
            check=True,
            text=True,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.STDOUT if capture else None,
        )
    except FileNotFoundError as exc:
        raise EngineError(f"command not found while running: {command_line(argv)}") from exc
    except subprocess.CalledProcessError as exc:
        if exc.stdout:
            print(exc.stdout, end="")
        raise EngineError(f"command failed with exit code {exc.returncode}: {command_line(argv)}") from exc


def capture_command(argv: Sequence[object], *, cwd: Path, env: dict[str, str] | None = None) -> str:
    """Run a command and return its output."""
    completed = run(argv, cwd=cwd, env=env, capture=True)
    return completed.stdout.strip()


def capture_command_quiet(
    argv: Sequence[object],
    *,
    cwd: Path,
    env: dict[str, str] | None = None,
) -> tuple[int, str]:
    """Run a command quietly and return exit code and output."""
    try:
        completed = subprocess.run(
            [str(arg) for arg in argv],
            cwd=str(cwd),
            env=env,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    except FileNotFoundError:
        return 127, ""
    return completed.returncode, completed.stdout.strip()


def venv_dir(root: Path) -> Path:
    """Return the virtual environment directory path."""
    return root / "out" / "host-tools" / "venv"


def venv_bin_dir(root: Path) -> Path:
    """Return the virtual environment bin directory path."""
    return venv_dir(root) / ("Scripts" if os.name == "nt" else "bin")


def host_tools_bin_dir(root: Path) -> Path:
    return root / "out" / "host-tools" / "bin"


def venv_python(root: Path) -> Path:
    """Return the path to the Python executable in the virtual environment."""
    return venv_bin_dir(root) / ("python.exe" if os.name == "nt" else "python")


def managed_executable(root: Path, name: str) -> Path:
    """Return the path to a managed executable in the virtual environment."""
    suffix = ".exe" if os.name == "nt" else ""
    return venv_bin_dir(root) / f"{name}{suffix}"


def tool_env(root: Path) -> dict[str, str]:
    env = os.environ.copy()
    path_entries = [host_tools_bin_dir(root), venv_bin_dir(root)]
    env["PATH"] = os.pathsep.join(str(path) for path in path_entries) + os.pathsep + env.get("PATH", "")
    env["CONAN_HOME"] = str(root / "out" / "conan" / "home")
    env["CONAN_NON_INTERACTIVE"] = "1"
    return env


def managed_package_version(root: Path, package_name: str) -> str:
    python = venv_python(root)
    if not python.exists():
        return ""

    script = (
        "from importlib import metadata; "
        f"print(metadata.version({package_name!r}))"
    )
    return_code, output = capture_command_quiet([python, "-c", script], cwd=root)
    if return_code != 0:
        return ""
    return output


def venv_has_pip(root: Path) -> bool:
    python = venv_python(root)
    if not python.exists():
        return False
    completed = subprocess.run(
        [str(python), "-m", "pip", "--version"],
        cwd=str(root),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
        check=False,
    )
    return completed.returncode == 0


def system_python_has_venv_support() -> bool:
    try:
        import ensurepip  # noqa: F401
    except ModuleNotFoundError:
        return False
    return True


def check_current_python(_root: Path, minimum: str) -> None:
    actual = platform.python_version()
    if not version_at_least(actual, minimum):
        print_missing_tool_help("Python", actual, sys.executable, minimum)
        raise EngineError(f"Python {minimum} or newer is required")
    print(f"Python {actual} satisfies minimum {minimum}: {sys.executable}")


def create_or_update_venv(root: Path) -> None:
    directory = venv_dir(root)
    directory.parent.mkdir(parents=True, exist_ok=True)
    if directory.is_dir() and venv_has_pip(root):
        print(f"Project-managed virtual environment is already usable: {directory}")
        return

    print(f"Creating or updating project-managed virtual environment: {directory}")
    try:
        import ensurepip  # noqa: F401
    except ModuleNotFoundError as exc:
        print("")
        print("Missing required Python virtual-environment support.")
        print(f"Detected host: {platform.platform()}")
        print(f"Detected Python: {platform.python_version()} at {sys.executable}")
        print("Ubuntu install command:")
        print("  sudo apt-get update && sudo apt-get install -y python3-venv")
        print("Rerun after installing prerequisites:")
        print("  ./init.sh")
        raise EngineError("python3-venv or ensurepip support is required") from exc

    try:
        venv.EnvBuilder(with_pip=True, clear=False, upgrade=False).create(directory)
    except Exception as exc:
        print("")
        print("Failed to create the project-managed virtual environment.")
        print(f"Detected host: {platform.platform()}")
        print(f"Target environment: {directory}")
        print("Ubuntu install command:")
        print("  sudo apt-get update && sudo apt-get install -y python3-venv")
        print("Rerun after installing prerequisites:")
        print("  ./init.sh")
        raise EngineError("virtual environment creation failed") from exc


def install_managed_tools(root: Path, versions: dict[str, dict[str, str]]) -> None:
    managed = versions["managed"]
    packages = [
        f"cmake=={managed['cmake']}",
        f"ninja=={managed['ninja']}",
        f"conan=={managed['conan']}",
    ]
    print("Network access: installing pinned Python packages for CMake, Ninja, and Conan.")
    run(
        [
            venv_python(root),
            "-m",
            "pip",
            "install",
            "--disable-pip-version-check",
            "--quiet",
            "--upgrade",
            *packages,
        ],
        cwd=root,
    )


def find_executable(candidates: Sequence[str]) -> str:
    for candidate in candidates:
        found = shutil.which(candidate)
        if found:
            return found
    return ""


def find_satisfying_executable(
    root: Path,
    candidates: Sequence[str],
    version_args: Sequence[str],
    minimum_version: str,
) -> str:
    for candidate in candidates:
        path = str(candidate)
        if not Path(path).is_file():
            found = shutil.which(path)
            if not found:
                continue
            path = found

        version = executable_version(path, version_args, root)
        if version and version_at_least(version, minimum_version):
            return path
    return ""


def link_or_replace_symlink(link_path: Path, target_path: Path) -> None:
    link_path.parent.mkdir(parents=True, exist_ok=True)
    if link_path.is_symlink():
        if link_path.resolve() == target_path.resolve():
            return
        link_path.unlink()
    elif link_path.exists():
        raise EngineError(f"cannot create tool shim because a non-symlink already exists: {link_path}")
    link_path.symlink_to(target_path)


def configure_system_tool_shims(root: Path, versions: dict[str, dict[str, str]]) -> None:
    if os.name == "nt":
        return

    minimum = versions["minimum"]
    clang_major = version_tuple(minimum["clang"])[0] if version_tuple(minimum["clang"]) else 18
    specs: list[tuple[str, Sequence[str], str]] = [
        ("clang", (f"clang-{clang_major}", "clang"), minimum["clang"]),
        ("clang++", (f"clang++-{clang_major}", "clang++"), minimum["clang"]),
        ("ld.lld", (f"ld.lld-{clang_major}", "ld.lld"), minimum["lld"]),
        ("clang-format", (f"clang-format-{clang_major}", "clang-format"), minimum["clang-format"]),
        ("clang-tidy", (f"clang-tidy-{clang_major}", "clang-tidy"), minimum["clang-tidy"]),
    ]

    for shim_name, candidates, minimum_version in specs:
        target = find_satisfying_executable(root, candidates, ("--version",), minimum_version)
        if target:
            link_or_replace_symlink(host_tools_bin_dir(root) / shim_name, Path(target))


def executable_version(path: str, args: Sequence[str], root: Path) -> str:
    if not path:
        return ""
    return_code, output = capture_command_quiet([path, *args], cwd=root)
    if return_code != 0 or not output:
        return ""
    return output.splitlines()[0]


def system_tool_statuses(root: Path, versions: dict[str, dict[str, str]]) -> list[ToolStatus]:
    minimum = versions["minimum"]
    clang_major = version_tuple(minimum["clang"])[0] if version_tuple(minimum["clang"]) else 14
    shim_dir = host_tools_bin_dir(root)
    tool_specs: list[tuple[str, bool, Sequence[str], Sequence[str], str]] = [
        ("Git", True, ("git",), ("--version",), ""),
        ("Python", True, (sys.executable,), ("--version",), minimum["python"]),
        ("Clang", True, (str(shim_dir / "clang++"), f"clang++-{clang_major}", "clang++"), ("--version",), minimum["clang"]),
        ("LLD", True, (str(shim_dir / "ld.lld"), f"ld.lld-{clang_major}", "ld.lld"), ("--version",), minimum["lld"]),
        (
            "clang-format",
            True,
            (str(shim_dir / "clang-format"), f"clang-format-{clang_major}", "clang-format"),
            ("--version",),
            minimum["clang-format"],
        ),
        (
            "clang-tidy",
            True,
            (str(shim_dir / "clang-tidy"), f"clang-tidy-{clang_major}", "clang-tidy"),
            ("--version",),
            minimum["clang-tidy"],
        ),
        ("ccache", False, ("ccache",), ("--version",), ""),
        ("LLDB", False, (f"lldb-{clang_major}", "lldb"), ("--version",), ""),
        ("GDB", False, ("gdb",), ("--version",), ""),
        ("rr", False, ("rr",), ("--version",), ""),
        ("Valgrind", False, ("valgrind",), ("--version",), ""),
    ]

    statuses: list[ToolStatus] = []
    for name, required, candidates, version_args, minimum_version in tool_specs:
        path = candidates[0] if Path(candidates[0]).is_file() else find_executable(candidates)
        version = executable_version(path, version_args, root) if path else ""
        if name == "Python":
            version = platform.python_version()
            path = sys.executable
        ok = bool(path)
        if ok and minimum_version:
            ok = version_at_least(version, minimum_version)
        requirement = f">= {minimum_version}" if minimum_version else "present"
        statuses.append(ToolStatus(name, required, bool(path), version, path, requirement, ok))

    return statuses


def managed_tool_statuses(root: Path, versions: dict[str, dict[str, str]]) -> list[ToolStatus]:
    managed = versions["managed"]
    specs = [
        ("Project-managed CMake", "cmake", "cmake"),
        ("Project-managed Ninja", "ninja", "ninja"),
        ("Project-managed Conan", "conan", "conan"),
    ]

    statuses: list[ToolStatus] = []
    for label, executable, package_name in specs:
        path = managed_executable(root, executable)
        expected = managed[package_name]
        package_version = managed_package_version(root, package_name)
        found = path.exists() and bool(package_version)
        statuses.append(
            ToolStatus(
                label,
                True,
                found,
                package_version,
                str(path) if path.exists() else "",
                f"== {expected}",
                found and package_version == expected,
            )
        )
    return statuses


def project_state_statuses(root: Path) -> list[ToolStatus]:
    installed_profile = root / "out" / "conan" / "home" / "profiles" / "linux-clang-x86_64"
    source_profile = root / "config" / "conan" / "profiles" / "linux-clang-x86_64"
    lockfile = root / "conan.lock"
    environment = venv_dir(root)
    environment_ok = environment.is_dir() and venv_python(root).exists() and venv_has_pip(root)
    profile_installed = installed_profile.is_file()
    profile_current = (
        profile_installed
        and source_profile.is_file()
        and file_sha256(installed_profile) == file_sha256(source_profile)
    )
    return [
        ToolStatus(
            "Conan profile",
            True,
            profile_installed or source_profile.is_file(),
            "installed/current" if profile_current else "installed/stale" if profile_installed else "source" if source_profile.is_file() else "",
            str(installed_profile if installed_profile.is_file() else source_profile if source_profile.is_file() else ""),
            "installed and current",
            profile_current,
        ),
        ToolStatus(
            "Conan lockfile",
            True,
            lockfile.is_file(),
            "present" if lockfile.is_file() else "",
            str(lockfile) if lockfile.is_file() else "",
            "present",
            lockfile.is_file(),
        ),
        ToolStatus(
            "Project-managed virtual environment",
            True,
            environment.is_dir(),
            "pip available" if environment_ok else "missing pip" if environment.is_dir() else "",
            str(environment) if environment.is_dir() else "",
            "present",
            environment_ok,
        ),
    ]


def print_status_table(statuses: Sequence[ToolStatus]) -> None:
    rows = [
        (
            status.name,
            "found" if status.found else "missing",
            status.version or "-",
            status.path or "-",
            status.requirement,
            "yes" if status.ok else "no",
        )
        for status in statuses
    ]
    headers = ("Item", "Status", "Version", "Path", "Requirement", "Satisfies")
    widths = [len(header) for header in headers]
    for row in rows:
        for index, value in enumerate(row):
            widths[index] = max(widths[index], len(value))

    print(" | ".join(header.ljust(widths[index]) for index, header in enumerate(headers)))
    print("-+-".join("-" * width for width in widths))
    for row in rows:
        print(" | ".join(value.ljust(widths[index]) for index, value in enumerate(row)))


def print_missing_tool_help(name: str, version: str, path: str, minimum: str) -> None:
    print("")
    print(f"Missing or invalid required tool: {name}")
    print(f"Detected host: {platform.platform()}")
    print(f"Detected version: {version or 'not found'}")
    print(f"Detected path: {path or 'not found'}")
    print(f"Minimum required version: {minimum}")
    print("One-command onboarding command:")
    print("  ./init.sh")
    if name != "Python":
        major = version_tuple(minimum)[0] if version_tuple(minimum) else 18
        print("Manual Ubuntu fallback, if automatic sudo installation is unavailable:")
        print(
            "  sudo apt-get update && sudo apt-get install -y "
            f"git python3 python3-venv ca-certificates clang-{major} lld-{major} "
            f"clang-format-{major} clang-tidy-{major}"
        )
        print("  If apt cannot find those packages, enable Ubuntu universe first:")
        print("  sudo add-apt-repository -y universe && sudo apt-get update")


def llvm_major_version(versions: dict[str, dict[str, str]]) -> int:
    parts = version_tuple(versions["minimum"]["clang"])
    return parts[0] if parts else 18


def host_is_ubuntu() -> bool:
    os_release = read_os_release()
    return os_release.get("ID", "").lower() == "ubuntu"


def python_venv_package_candidates() -> tuple[str, ...]:
    return (f"python{sys.version_info.major}.{sys.version_info.minor}-venv", "python3-venv")


def required_ubuntu_package_groups(root: Path, versions: dict[str, dict[str, str]]) -> list[AptPackageGroup]:
    major = llvm_major_version(versions)
    groups = [
        AptPackageGroup("CA certificates for HTTPS package downloads", ("ca-certificates",)),
    ]

    for status in system_tool_statuses(root, versions):
        if not status.required or status.ok:
            continue
        if status.name == "Git":
            groups.append(AptPackageGroup("Git", ("git",)))
        elif status.name == "Clang":
            groups.append(AptPackageGroup("Clang", (f"clang-{major}",)))
        elif status.name == "LLD":
            groups.append(AptPackageGroup("LLD", (f"lld-{major}",)))
        elif status.name == "clang-format":
            groups.append(AptPackageGroup("clang-format", (f"clang-format-{major}",)))
        elif status.name == "clang-tidy":
            groups.append(AptPackageGroup("clang-tidy", (f"clang-tidy-{major}",)))

    if not system_python_has_venv_support() and not venv_has_pip(root):
        groups.append(AptPackageGroup("Python virtual-environment support", python_venv_package_candidates()))

    return groups


def apt_package_has_candidate(root: Path, package: str) -> bool:
    return_code, output = capture_command_quiet(["apt-cache", "policy", package], cwd=root)
    if return_code != 0:
        return False
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("Candidate:"):
            return stripped.partition(":")[2].strip() != "(none)"
    return False


def resolve_apt_package_groups(root: Path, groups: Sequence[AptPackageGroup]) -> tuple[list[str], list[AptPackageGroup]]:
    selected: list[str] = []
    missing: list[AptPackageGroup] = []
    for group in groups:
        package = next((candidate for candidate in group.candidates if apt_package_has_candidate(root, candidate)), "")
        if package:
            selected.append(package)
        else:
            missing.append(group)
    return selected, missing


def print_missing_apt_packages(root: Path, groups: Sequence[AptPackageGroup]) -> None:
    print("")
    print("Apt could not find installable packages for required prerequisites.")
    print(f"Detected host: {platform.platform()}")
    print("Missing package groups:")
    for group in groups:
        print(f"  {group.purpose}: {' or '.join(group.candidates)}")
        for candidate in group.candidates:
            return_code, output = capture_command_quiet(["apt-cache", "policy", candidate], cwd=root)
            if return_code == 0 and output:
                compact = "; ".join(line.strip() for line in output.splitlines() if line.strip())
                print(f"    apt-cache policy {candidate}: {compact}")
    if host_is_ubuntu():
        print("The standard Ubuntu 'universe' component may be disabled or unavailable from this mirror.")


def ubuntu_sources_file() -> Path:
    return Path("/etc/apt/sources.list.d/ubuntu.sources")


def ubuntu_stanza_has_component(stanza: str, component: str) -> bool:
    if re.search(r"(?im)^Enabled:\s*no\s*$", stanza):
        return False
    if not re.search(r"(?im)^Types:\s*.*\bdeb\b", stanza):
        return False
    if not re.search(r"(?im)^URIs:\s*.*ubuntu", stanza):
        return False
    components_match = re.search(r"(?im)^Components:\s*(.+)$", stanza)
    if not components_match:
        return False
    return component in components_match.group(1).split()


def ubuntu_apt_component_enabled(component: str) -> bool:
    source_path = ubuntu_sources_file()
    if source_path.is_file():
        text = source_path.read_text(encoding="utf-8")
        if any(ubuntu_stanza_has_component(stanza, component) for stanza in re.split(r"\n\s*\n", text)):
            return True

    list_path = Path("/etc/apt/sources.list")
    if list_path.is_file():
        for raw_line in list_path.read_text(encoding="utf-8").splitlines():
            line = raw_line.strip()
            if not line or line.startswith("#") or not line.startswith("deb "):
                continue
            if "ubuntu" in line and component in line.split():
                return True
    return False


def add_component_to_ubuntu_sources_text(text: str, component: str) -> tuple[str, bool]:
    stanzas = re.split(r"(\n\s*\n)", text)
    changed = False
    updated_parts: list[str] = []

    for part in stanzas:
        if not part.strip():
            updated_parts.append(part)
            continue
        if re.search(r"(?im)^Enabled:\s*no\s*$", part):
            updated_parts.append(part)
            continue
        if not re.search(r"(?im)^Types:\s*.*\bdeb\b", part) or not re.search(r"(?im)^URIs:\s*.*ubuntu", part):
            updated_parts.append(part)
            continue

        def replace_components(match: re.Match[str]) -> str:
            nonlocal changed
            components = match.group(1).split()
            if component in components:
                return match.group(0)
            changed = True
            return "Components: " + " ".join([*components, component])

        updated_parts.append(re.sub(r"(?im)^Components:\s*(.+)$", replace_components, part))

    return "".join(updated_parts), changed


def patch_ubuntu_sources_component(root: Path, prefix: Sequence[str], component: str) -> bool:
    source_path = ubuntu_sources_file()
    if not source_path.is_file():
        return False

    original = source_path.read_text(encoding="utf-8")
    updated, changed = add_component_to_ubuntu_sources_text(original, component)
    if not changed:
        return False

    staging_path = root / "out" / "generated" / "apt" / source_path.name
    staging_path.parent.mkdir(parents=True, exist_ok=True)
    staging_path.write_text(updated, encoding="utf-8")

    backup_path = source_path.with_name(source_path.name + ".ludus-backup")
    print(f"Patching Ubuntu apt source to enable '{component}': {source_path}")
    print(f"Original source backup, if not already present: {backup_path}")
    run([*prefix, "cp", "-n", source_path, backup_path], cwd=root)
    run([*prefix, "install", "-m", "0644", staging_path, source_path], cwd=root)
    return True


def enable_ubuntu_universe(root: Path, prefix: Sequence[str]) -> None:
    if not host_is_ubuntu():
        return
    if ubuntu_apt_component_enabled("universe"):
        return

    print("Ubuntu package component 'universe' is needed for LLVM/Python development packages.")
    if not shutil.which("add-apt-repository"):
        run([*prefix, "apt-get", "install", "-y", "software-properties-common"], cwd=root)

    repository_tool = shutil.which("add-apt-repository") or "add-apt-repository"
    run([*prefix, repository_tool, "-y", "universe"], cwd=root)
    if not ubuntu_apt_component_enabled("universe"):
        if not patch_ubuntu_sources_component(root, prefix, "universe"):
            raise EngineError("could not enable Ubuntu universe package component")


def read_os_release() -> dict[str, str]:
    path = Path("/etc/os-release")
    if not path.is_file():
        return {}

    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        if "=" not in raw_line:
            continue
        key, value = raw_line.split("=", 1)
        values[key] = value.strip().strip('"')
    return values


def host_supports_apt_install() -> bool:
    if platform.system() != "Linux":
        return False
    os_release = read_os_release()
    distro_markers = " ".join([os_release.get("ID", ""), os_release.get("ID_LIKE", "")]).lower()
    return bool({"ubuntu", "debian"} & set(distro_markers.split())) and shutil.which("apt-get") is not None


def sudo_command_prefix() -> list[str]:
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        return []
    sudo = shutil.which("sudo")
    if not sudo:
        raise EngineError("automatic system package installation needs sudo or a root shell")
    return [sudo]


def system_prerequisites_need_install(root: Path, versions: dict[str, dict[str, str]]) -> bool:
    required_tools_ok = all(status.ok for status in system_tool_statuses(root, versions) if status.required)
    venv_support_ok = system_python_has_venv_support() or venv_has_pip(root)
    return not required_tools_ok or not venv_support_ok


def install_ubuntu_system_prerequisites(root: Path, versions: dict[str, dict[str, str]]) -> None:
    configure_system_tool_shims(root, versions)
    if not system_prerequisites_need_install(root, versions):
        print("Ubuntu system prerequisites already satisfy Milestone 0.")
        return

    if not host_supports_apt_install():
        raise EngineError("automatic system prerequisite installation currently supports apt-based Ubuntu/Debian hosts")

    prefix = sudo_command_prefix()
    print("Installing Ubuntu system prerequisites. This may ask for your sudo password once.")
    package_groups = required_ubuntu_package_groups(root, versions)
    run([*prefix, "apt-get", "update"], cwd=root)

    packages, missing_groups = resolve_apt_package_groups(root, package_groups)
    if missing_groups and host_is_ubuntu():
        enable_ubuntu_universe(root, prefix)
        run([*prefix, "apt-get", "update"], cwd=root)
        packages, missing_groups = resolve_apt_package_groups(root, package_groups)

    if missing_groups:
        print_missing_apt_packages(root, missing_groups)
        raise EngineError("apt prerequisites are unavailable from the configured package sources")

    print("Packages: " + " ".join(packages))
    run([*prefix, "apt-get", "install", "-y", *packages], cwd=root)
    configure_system_tool_shims(root, versions)


def validate_required_system_tools(root: Path, versions: dict[str, dict[str, str]]) -> None:
    statuses = [status for status in system_tool_statuses(root, versions) if status.required]
    failures = [status for status in statuses if not status.ok]
    if not failures:
        return

    print_status_table(statuses)
    for failure in failures:
        minimum = versions["minimum"].get(failure.name.lower(), failure.requirement.removeprefix(">= "))
        print_missing_tool_help(failure.name, failure.version, failure.path, minimum)
    raise EngineError("required system-level tools are missing or below the supported version")


def validate_managed_tools(root: Path, versions: dict[str, dict[str, str]]) -> None:
    failures = [status for status in managed_tool_statuses(root, versions) if not status.ok]
    if failures:
        print_status_table(managed_tool_statuses(root, versions))
        raise EngineError("project-managed tools are missing or do not match pinned versions")


def install_conan_profile(root: Path) -> Path:
    source = root / "config" / "conan" / "profiles" / "linux-clang-x86_64"
    destination = root / "out" / "conan" / "home" / "profiles" / "linux-clang-x86_64"
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    print(f"Installed Conan profile: {destination}")
    return destination


def conan(root: Path) -> Path:
    return managed_executable(root, "conan")


def cmake(root: Path) -> Path:
    return managed_executable(root, "cmake")


def ninja(root: Path) -> Path:
    return managed_executable(root, "ninja")


def clang_cxx(root: Path) -> Path:
    return host_tools_bin_dir(root) / ("clang++.exe" if os.name == "nt" else "clang++")


def prepare_conan_artifacts(
    root: Path,
    versions: dict[str, dict[str, str]],
    presets: Sequence[str],
) -> None:
    check_current_python(root, versions["minimum"]["python"])
    create_or_update_venv(root)
    install_managed_tools(root, versions)
    validate_managed_tools(root, versions)
    configure_system_tool_shims(root, versions)
    validate_required_system_tools(root, versions)
    profile_path = install_conan_profile(root)
    create_conan_lock(root, profile_path)
    for preset in presets:
        conan_install_for_preset(root, profile_path, preset)


def cmake_cache_value(cache_path: Path, name: str) -> str | None:
    if not cache_path.is_file():
        return None
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(f"{name}:"):
            return line.partition("=")[2]
    return None


def cmake_cache_repair_reasons(root: Path, preset: str) -> list[str]:
    cache_path = build_dir_for_preset(root, preset) / "CMakeCache.txt"
    if not cache_path.is_file():
        return []

    expected_compiler = str(clang_cxx(root))
    expected_ninja = str(ninja(root))
    expected_toolchain = str(root / "out" / "conan" / preset / "conan_toolchain.cmake")
    checks = (
        ("CMAKE_CXX_COMPILER", expected_compiler),
        ("CMAKE_MAKE_PROGRAM", expected_ninja),
        ("CMAKE_TOOLCHAIN_FILE", expected_toolchain),
    )

    reasons: list[str] = []
    for variable, expected in checks:
        actual = cmake_cache_value(cache_path, variable)
        if actual is None:
            reasons.append(f"{variable} is missing")
        elif actual.endswith("-NOTFOUND"):
            reasons.append(f"{variable} is {actual}")
        elif actual != expected:
            reasons.append(f"{variable} is {actual}, expected {expected}")

    generator = cmake_cache_value(cache_path, "CMAKE_GENERATOR")
    if generator is not None and generator != "Ninja":
        reasons.append(f"CMAKE_GENERATOR is {generator}, expected Ninja")

    return reasons


def repair_existing_cmake_caches(root: Path, presets: Sequence[str]) -> None:
    for preset in presets:
        reasons = cmake_cache_repair_reasons(root, preset)
        if not reasons:
            continue
        print(f"Refreshing stale CMake cache for {preset}:")
        for reason in reasons:
            print(f"  - {reason}")
        run([cmake(root), "--fresh", "-U", "Catch2_DIR", "--preset", preset], cwd=root, env=tool_env(root))


def create_conan_lock(root: Path, profile_path: Path) -> None:
    print("Network access: resolving the Conan dependency graph and updating conan.lock.")
    env = tool_env(root)
    lock_work_dir = root / "out" / "conan" / "lock-work"
    lock_work_dir.mkdir(parents=True, exist_ok=True)
    lockfile = root / "conan.lock"
    backup = lock_work_dir / "conan.lock.previous"
    had_lockfile = lockfile.exists()
    if had_lockfile:
        shutil.copy2(lockfile, backup)
        lockfile.unlink()

    try:
        run(
            [
                conan(root),
                "lock",
                "create",
                str(root),
                "--profile:host",
                str(profile_path),
                "--profile:build",
                str(profile_path),
                "--settings:host",
                "build_type=Release",
                "--settings:build",
                "build_type=Release",
                "--lockfile-out",
                str(lockfile),
            ],
            cwd=lock_work_dir,
            env=env,
            capture=True,
        )
    except Exception:
        if had_lockfile and backup.exists() and not lockfile.exists():
            shutil.copy2(backup, lockfile)
        raise


def conan_install_for_preset(root: Path, profile_path: Path, preset: str) -> None:
    build_type = PRESET_BUILD_TYPES[preset]
    output_dir = root / "out" / "conan" / preset
    output_dir.mkdir(parents=True, exist_ok=True)
    print(f"Network access: populating Conan cache and generator files for {preset}.")
    print("Conan may build missing third-party packages such as Catch2; this does not build Ludus engine targets.")
    run(
        [
            conan(root),
            "install",
            str(root),
            "--output-folder",
            str(output_dir),
            "--profile:host",
            str(profile_path),
            "--profile:build",
            str(profile_path),
            "--settings:host",
            f"build_type={build_type}",
            "--settings:build",
            "build_type=Release",
            "--lockfile",
            str(root / "conan.lock"),
            "--lockfile-partial",
            "--build=missing",
        ],
        cwd=root,
        env=tool_env(root),
        capture=True,
    )
    write_bootstrap_marker(root, preset)


def build_dir_for_preset(root: Path, preset: str) -> Path:
    return root / "out" / "build" / preset


def configure_needs_fresh_cache(root: Path, preset: str) -> bool:
    cache_path = build_dir_for_preset(root, preset) / "CMakeCache.txt"
    if not cache_path.is_file():
        return False
    cache_text = cache_path.read_text(encoding="utf-8", errors="replace")
    stale_package_cache_entries = ("Catch2_DIR:PATH=Catch2_DIR-NOTFOUND",)
    return any(entry in cache_text for entry in stale_package_cache_entries) or bool(cmake_cache_repair_reasons(root, preset))


def cmake_configure(root: Path, preset: str, extra_args: Sequence[str] = ()) -> None:
    args: list[object] = [cmake(root)]
    if configure_needs_fresh_cache(root, preset):
        print(f"Refreshing stale CMake cache for {preset}.")
        args.append("--fresh")
    args.extend(["-U", "Catch2_DIR", "--preset", preset, *extra_args])
    run(args, cwd=root, env=tool_env(root))


def cmake_build(root: Path, preset: str, extra_args: Sequence[str] = ()) -> None:
    run([cmake(root), "--build", "--preset", preset, *extra_args], cwd=root, env=tool_env(root))


def ctest(root: Path, preset: str, label: str | None = None) -> None:
    args: list[object] = [cmake(root), "--build", "--preset", preset]
    run(args, cwd=root, env=tool_env(root))
    test_args: list[object] = [cmake(root), "-E", "env", "CTEST_OUTPUT_ON_FAILURE=1", managed_executable(root, "ctest"), "--preset", preset]
    if label:
        test_args.extend(["-L", label])
    run(test_args, cwd=root, env=tool_env(root))


def ensure_known_preset(preset: str) -> None:
    if preset not in PRESET_BUILD_TYPES:
        known = ", ".join(PRESET_BUILD_TYPES)
        raise EngineError(f"unknown preset '{preset}'. Known presets: {known}")


def ensure_bootstrap_for_preset(root: Path, preset: str) -> None:
    ensure_known_preset(preset)
    missing: list[Path] = [
        path
        for path in (
            cmake(root),
            ninja(root),
            conan(root),
            root / "conan.lock",
            root / "out" / "conan" / preset / "conan_toolchain.cmake",
        )
        if not path.exists()
    ]
    if missing:
        for path in missing:
            print(f"Missing bootstrap artifact: {path}")
        raise EngineError(f"bootstrap has not completed for this preset; run ./init.sh")
    if not bootstrap_marker_is_current(root, preset):
        print(f"Stale bootstrap artifact: {bootstrap_marker_path(root, preset)}")
        raise EngineError(f"bootstrap artifacts are stale; run ./init.sh")


def command_bootstrap(args: argparse.Namespace) -> int:
    root = repo_root()
    versions = load_tool_versions(root)
    prepare_conan_artifacts(root, versions, tuple(PRESET_BUILD_TYPES))
    repair_existing_cmake_caches(root, tuple(PRESET_BUILD_TYPES))
    print("Running minimal CMake configuration smoke test for linux-clang-development.")
    cmake_configure(root, DEFAULT_PRESET)
    if getattr(args, "show_next_commands", True):
        print("")
        print("Bootstrap complete. Next commands:")
        print("  ./scripts/build")
        print("  ./scripts/test")
        print("  ./scripts/install-sdk")
    return 0


def command_init(args: argparse.Namespace) -> int:
    root = repo_root()
    versions = load_tool_versions(root)
    preset = args.preset
    ensure_known_preset(preset)
    run_validation = args.validate or args.ci
    if args.all_presets and args.preset_only:
        raise EngineError("--all-presets and --preset-only describe conflicting initialization scopes")

    if args.ci:
        os.environ["CI"] = "true"

    print("Ludus one-command onboarding")
    print(f"Repository: {root}")
    print(f"Preset: {preset}")

    check_current_python(root, versions["minimum"]["python"])
    if args.no_system_install:
        print("Skipping automatic system package installation because --no-system-install was supplied.")
        configure_system_tool_shims(root, versions)
    else:
        install_ubuntu_system_prerequisites(root, versions)

    if args.preset_only and not run_validation:
        presets = (preset,)
    else:
        presets = tuple(PRESET_BUILD_TYPES)
    prepare_conan_artifacts(root, versions, presets)
    repair_existing_cmake_caches(root, presets)
    if command_doctor(argparse.Namespace()) != 0:
        raise EngineError("doctor reported an invalid required tool or project state")

    if run_validation:
        command_build(argparse.Namespace(preset=preset, extra=[]))
        command_test(argparse.Namespace(preset=preset, label=None))

        if not args.skip_checks:
            command_check(argparse.Namespace(preset=preset, format=False, tidy=False, all=True, fix=False))

        if not args.skip_sanitizers:
            command_build(argparse.Namespace(preset="linux-clang-asan-ubsan", extra=[]))
            command_test(argparse.Namespace(preset="linux-clang-asan-ubsan", label=None))

        if not args.skip_sdk:
            command_install_sdk(argparse.Namespace(preset=preset))

    print("")
    if run_validation:
        print("Ludus initialization and validation complete.")
    else:
        print("Ludus initialization complete. No engine targets were built.")
        print("Prepared presets: " + ", ".join(presets))
        print("VS Code CMake Tools is configured to use:")
        print(f"  {cmake(root)}")
        print("If VS Code was already open, reload the window before using the CMake Tools Build button.")
        print("Build from VS Code CMake Tools, or run:")
        print("  ./scripts/build")
        print("Run the full build/test/SDK validation with:")
        print("  ./init.sh --validate")
    return 0


def command_doctor(_args: argparse.Namespace) -> int:
    root = repo_root()
    versions = load_tool_versions(root)
    statuses = [
        *[status for status in system_tool_statuses(root, versions) if status.required],
        *managed_tool_statuses(root, versions),
        *project_state_statuses(root),
        *[status for status in system_tool_statuses(root, versions) if not status.required],
    ]
    print_status_table(statuses)
    return 1 if any(status.required and not status.ok for status in statuses) else 0


def command_build(args: argparse.Namespace) -> int:
    root = repo_root()
    ensure_bootstrap_for_preset(root, args.preset)
    cmake_configure(root, args.preset)
    cmake_build(root, args.preset, args.extra)
    return 0


def command_test(args: argparse.Namespace) -> int:
    root = repo_root()
    ensure_bootstrap_for_preset(root, args.preset)
    cmake_configure(root, args.preset)
    ctest(root, args.preset, args.label)
    return 0


def source_files(root: Path, suffixes: set[str], roots: Iterable[str]) -> list[Path]:
    ignored_parts = {".git", "out", "__pycache__"}
    files: list[Path] = []
    for relative_root in roots:
        base = root / relative_root
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if not path.is_file() or path.suffix not in suffixes:
                continue
            if any(part in ignored_parts for part in path.relative_to(root).parts):
                continue
            files.append(path)
    return sorted(files)


def find_system_tool(root: Path, name: str, versions: dict[str, dict[str, str]]) -> str:
    for status in system_tool_statuses(root, versions):
        if status.name == name:
            if status.ok:
                return status.path
            minimum = versions["minimum"].get(name.lower(), "")
            print_missing_tool_help(name, status.version, status.path, minimum)
            raise EngineError(f"{name} is required")
    raise EngineError(f"unknown tool status requested: {name}")


def run_format_check(root: Path, *, fix: bool) -> None:
    versions = load_tool_versions(root)
    clang_format = find_system_tool(root, "clang-format", versions)
    files = source_files(root, FORMAT_SUFFIXES, ("modules", "apps", "tests"))
    if not files:
        print("No source files found for clang-format.")
        return

    if fix:
        run([clang_format, "-i", *files], cwd=root)
    else:
        run([clang_format, "--dry-run", "-Werror", *files], cwd=root)


def compile_database_files(build_dir: Path) -> set[Path]:
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        raise EngineError(f"missing compilation database: {database}")
    try:
        entries = json.loads(database.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise EngineError(f"failed to parse {database}: {exc}") from exc
    return {Path(entry["file"]).resolve() for entry in entries if "file" in entry}


def run_tidy(root: Path, preset: str) -> None:
    versions = load_tool_versions(root)
    clang_tidy = find_system_tool(root, "clang-tidy", versions)
    build_dir = root / "out" / "build" / preset
    if not (build_dir / "compile_commands.json").is_file():
        cmake_configure(root, preset)

    compiled_files = compile_database_files(build_dir)
    requested_files = [path.resolve() for path in source_files(root, {".cpp", ".cc", ".cxx"}, TIDY_ROOTS)]
    tidy_files = [path for path in requested_files if path in compiled_files]

    if not tidy_files:
        print("No project source files from modules/apps were found in the compilation database.")
        return

    header_filter = f"^{re.escape(str(root))}/(modules|apps)/.*"
    for path in tidy_files:
        run(
            [
                clang_tidy,
                "-p",
                build_dir,
                "--quiet",
                "--warnings-as-errors=*",
                f"--header-filter={header_filter}",
                path,
            ],
            cwd=root,
        )


def command_check(args: argparse.Namespace) -> int:
    root = repo_root()
    ensure_bootstrap_for_preset(root, args.preset)
    run_all = args.all or not args.format and not args.tidy

    if args.format or run_all:
        run_format_check(root, fix=args.fix)
    if args.tidy or run_all:
        cmake_configure(root, args.preset)
        run_tidy(root, args.preset)
    return 0


def verify_sdk_install(root: Path, prefix: Path) -> None:
    required_paths = [
        prefix / "include" / "ludus" / "foundation" / "base" / "version.hpp",
        prefix / "include" / "ludus" / "foundation" / "base" / "build_metadata.hpp",
        prefix / "lib" / "libludus_foundation_base.a",
        prefix / "lib" / "cmake" / "Ludus" / "LudusTargets.cmake",
        prefix / "lib" / "cmake" / "Ludus" / "LudusConfig.cmake",
        prefix / "lib" / "cmake" / "Ludus" / "LudusConfigVersion.cmake",
        prefix / "share" / "Ludus" / "licenses" / "LICENSE",
        prefix / "share" / "Ludus" / "LudusSdkManifest.json",
    ]

    missing = [path for path in required_paths if not path.exists()]
    if missing:
        for path in missing:
            print(f"Missing installed SDK artifact: {path}")
        raise EngineError("installed SDK is incomplete")

    private_headers = [path for path in (prefix / "include").rglob("*") if path.is_file() and "internal" in path.parts]
    if private_headers:
        for path in private_headers:
            print(f"Private header leaked into SDK install: {path}")
        raise EngineError("private headers must not be installed")

    forbidden_roots = [root / "modules", root / "apps", root / "cmake"]
    package_files = list((prefix / "lib" / "cmake" / "Ludus").glob("*.cmake"))
    for package_file in package_files:
        text = package_file.read_text(encoding="utf-8")
        for forbidden in forbidden_roots:
            if str(forbidden) in text:
                raise EngineError(f"installed CMake package references source-tree path {forbidden} in {package_file}")

    print(f"Installed SDK artifacts verified: {prefix}")


def command_install_sdk(args: argparse.Namespace) -> int:
    root = repo_root()
    preset = args.preset
    ensure_bootstrap_for_preset(root, preset)
    cmake_configure(root, preset)
    cmake_build(root, preset)

    build_dir = root / "out" / "build" / preset
    prefix = root / "out" / "install" / preset
    run([cmake(root), "--install", build_dir, "--prefix", prefix], cwd=root, env=tool_env(root))
    verify_sdk_install(root, prefix)

    consumer_source = root / "tests" / "sdk_consumer"
    consumer_build = root / "out" / "build" / "sdk-consumer" / preset
    run(
        [
            cmake(root),
            "-S",
            consumer_source,
            "-B",
            consumer_build,
            "-G",
            "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={ninja(root)}",
            f"-DCMAKE_CXX_COMPILER={clang_cxx(root)}",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_PREFIX_PATH={prefix}",
        ],
        cwd=root,
        env=tool_env(root),
    )
    run([cmake(root), "--build", consumer_build], cwd=root, env=tool_env(root))
    run([consumer_build / "ludus_sdk_consumer"], cwd=root, env=tool_env(root))
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Ludus development orchestration")
    subparsers = parser.add_subparsers(dest="command", required=True)

    init_parser = subparsers.add_parser("init", help="install prerequisites and prepare local build dependencies")
    init_parser.add_argument("preset", nargs="?", default=DEFAULT_PRESET)
    init_parser.add_argument("--no-system-install", action="store_true", help="do not install Ubuntu packages automatically")
    init_parser.add_argument("--all-presets", action="store_true", help="prepare Conan files for every committed preset (default)")
    init_parser.add_argument("--preset-only", action="store_true", help="prepare only the selected preset")
    init_parser.add_argument("--validate", "--full", action="store_true", help="also build, test, check, and validate the SDK")
    init_parser.add_argument("--skip-checks", action="store_true", help="skip clang-format and clang-tidy checks during validation")
    init_parser.add_argument("--skip-sanitizers", action="store_true", help="skip the ASan/UBSan preset during validation")
    init_parser.add_argument("--skip-sdk", action="store_true", help="skip the installed SDK consumer test during validation")
    init_parser.add_argument("--ci", action="store_true", help="run validation with CI=true for warnings-as-errors")
    init_parser.set_defaults(func=command_init)

    bootstrap_parser = subparsers.add_parser("bootstrap", help="install managed tools and prepare Conan/CMake")
    bootstrap_parser.set_defaults(func=command_bootstrap)

    doctor_parser = subparsers.add_parser("doctor", help="diagnose host and project tool state")
    doctor_parser.set_defaults(func=command_doctor)

    build_parser = subparsers.add_parser("build", help="configure and build a preset")
    build_parser.add_argument("preset", nargs="?", default=DEFAULT_PRESET)
    build_parser.add_argument("extra", nargs=argparse.REMAINDER)
    build_parser.set_defaults(func=command_build)

    test_parser = subparsers.add_parser("test", help="build tests and run CTest")
    test_parser.add_argument("preset", nargs="?", default=DEFAULT_PRESET)
    test_parser.add_argument("--label", "-L", help="CTest label regex")
    test_parser.set_defaults(func=command_test)

    check_parser = subparsers.add_parser("check", help="run formatting and clang-tidy checks")
    check_parser.add_argument("preset", nargs="?", default=DEFAULT_PRESET)
    check_parser.add_argument("--format", action="store_true", help="verify clang-format")
    check_parser.add_argument("--tidy", action="store_true", help="run targeted clang-tidy")
    check_parser.add_argument("--all", action="store_true", help="run all checks")
    check_parser.add_argument("--fix", action="store_true", help="allow clang-format to update files")
    check_parser.set_defaults(func=command_check)

    install_parser = subparsers.add_parser("install-sdk", help="install the SDK and run the external consumer")
    install_parser.add_argument("preset", nargs="?", default=DEFAULT_PRESET)
    install_parser.set_defaults(func=command_install_sdk)

    return parser


def main(argv: Sequence[str]) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    parser = make_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except EngineError as exc:
        sys.stdout.flush()
        print(f"error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
