"""Read-only validation of a game's selectable CMake preset surface."""
from __future__ import annotations

import re
import subprocess
from pathlib import Path


_PRESET = re.compile(r'^\s+"([A-Za-z0-9_.+-]+)"(?:\s+-.*)?\s*$')


def list_presets(cmake: str, source_dir, env: dict[str, str], kind: str) -> set[str]:
    """Return CMake's actually selectable presets without configuring a tree."""
    if kind not in ("configure", "build", "test"):
        raise ValueError(f"unsupported CMake preset kind: {kind}")
    try:
        result = subprocess.run(
            [cmake, f"--list-presets={kind}"], cwd=str(source_dir), env=env,
            capture_output=True, text=True, timeout=15, check=False,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        raise ValueError(f"cannot run the selected CMake to list {kind} presets: {exc}") from exc
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()[:512]
        raise ValueError(f"CMake could not list {kind} presets: {detail}")
    names = set()
    for line in result.stdout.splitlines():
        match = _PRESET.match(line)
        if match:
            names.add(match.group(1))
    return names


def validate_project_presets(cmake: str, source_dir, env: dict[str, str], preset: str) -> None:
    """Require the selected configure/build/test preset in CMake's resolved view."""
    from .project_setup import preset_for
    preset = preset_for(Path(source_dir), preset)
    for kind in ("configure", "build", "test"):
        available = list_presets(cmake, source_dir, env, kind)
        if preset not in available:
            raise ValueError(
                f"CMake setup is missing a selectable {kind} preset {preset!r}; "
                "repair the project presets explicitly"
            )


def inspect_project_setup(cmake: str, ninja: str, source_dir: Path, build_dir: Path,
                          env: dict[str, str], preset: str, *, compiler: str | None = None,
                          sdk_prefix: str | None = None) -> None:
    """Check the selected native project setup without configuring or writing files."""
    validate_project_presets(cmake, source_dir, env, preset)
    if not Path(cmake).is_file():
        raise ValueError(f"selected CMake executable is missing: {cmake}")
    if not Path(ninja).is_file():
        raise ValueError(f"selected Ninja executable is missing: {ninja}")
    if compiler is not None and not Path(compiler).is_file():
        raise ValueError(f"selected C++ compiler is missing: {compiler}")
    if sdk_prefix is not None and not Path(sdk_prefix).is_dir():
        raise ValueError(f"selected Ludus SDK prefix is missing: {sdk_prefix}")

    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return
    values: dict[str, str] = {}
    try:
        for line in cache.read_text(encoding="utf-8").splitlines():
            if line.startswith("//") or line.startswith("#") or ":" not in line or "=" not in line:
                continue
            name, value = line.split("=", 1)
            key = name.split(":", 1)[0]
            values[key] = value
    except OSError as exc:
        raise ValueError(f"cannot read CMake cache: {exc}") from exc

    stale: list[str] = []
    if values.get("CMAKE_GENERATOR") != "Ninja":
        stale.append("generator is missing or changed (expected Ninja)")
    cached_ninja = values.get("CMAKE_MAKE_PROGRAM")
    if not cached_ninja or Path(cached_ninja).resolve() != Path(ninja).resolve():
        stale.append("Ninja executable is missing or changed")
    cached_compiler = values.get("CMAKE_CXX_COMPILER")
    if compiler is not None and (
        not cached_compiler or Path(cached_compiler).resolve() != Path(compiler).resolve()
    ):
        stale.append("C++ compiler is missing or changed")
    cached_prefix = values.get("CMAKE_PREFIX_PATH")
    if sdk_prefix is not None:
        cached_prefixes = [Path(part).resolve() for part in cached_prefix.split(";") if part] if cached_prefix else []
        if Path(sdk_prefix).resolve() not in cached_prefixes:
            stale.append("Ludus SDK prefix is missing or changed")
    if stale:
        raise ValueError("stale CMake cache: " + ", ".join(stale) + "; configure explicitly to refresh it")
