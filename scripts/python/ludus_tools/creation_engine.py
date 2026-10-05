"""Automatic engine selection for explicit, staged CLI/Editor creation.

Discovery reads only named locations; preparation runs only after Create.
An explicit SDK/environment override is never silently replaced on failure.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

from .errors import INVALID_PROJECT, ToolingError
from .identity import load_manifest_file, load_prefix_manifest

PROFILE = "linux-clang-development"


def prepared_sdk_prefix(root: Path) -> Path:
    # Separate ABI variants without removing SDKs used by existing projects.
    # Hash the build's actual identity, never a guessed assertion/toolchain key.
    identity = load_manifest_file(root / "out/build" / PROFILE / "cmake/LudusSdkManifest.json")
    key = json.dumps(identity.abi_key(), sort_keys=True).encode("utf-8")
    suffix = hashlib.sha256(key).hexdigest()[:16]
    return root / "out/install" / f"{PROFILE}-{suffix}"


def validate_creation_sdk(prefix: Path):
    identity = load_prefix_manifest(prefix)
    if (identity.target_triple != "x86_64-linux-gnu" or identity.flavor != "Development"
            or "FoundationBase" not in identity.components):
        raise ToolingError(INVALID_PROJECT, "Select a native Development SDK containing FoundationBase")
    return identity


def source_revision(root: Path) -> str:
    # Trusted editor/CLI tooling checkout, never a game-owned script or hook.
    try:
        result = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"],
                                capture_output=True, text=True, timeout=10, check=False)
        return result.stdout.strip() if result.returncode == 0 else ""
    except (OSError, subprocess.TimeoutExpired):
        return ""


def select_creation_sdk(root: Path, *, explicit: Path | None = None,
                        prepare=None, environ=None) -> Path:
    """Explicit > environment > current editor install > prepare editor engine.

    ABI/compiler and real configure/build/test validation still run in repair;
    discovery alone never reports a project ready or publishes a destination.
    """
    env = os.environ if environ is None else environ
    override = env.get("LUDUS_SDK_PREFIX", "").strip()
    if explicit is not None or override:
        prefix = Path(explicit) if explicit is not None else Path(override)
        if explicit is None and not prefix.is_absolute():
            raise ToolingError(INVALID_PROJECT, "LUDUS_SDK_PREFIX must be an absolute SDK directory")
        prefix = prefix.resolve()
        validate_creation_sdk(prefix)
        return prefix
    root = root.resolve()
    prefix = root / "out/install" / PROFILE
    try:
        cmake = (root / "CMakeLists.txt").read_text()
    except OSError as exc:
        raise ToolingError("MissingTools", "Cannot identify the editor engine checkout") from exc
    version = re.search(r"project\([^\n]*\bVERSION\s+([0-9.]+)", cmake)
    if not version:
        raise ToolingError("MissingTools", "Cannot identify the editor engine version")
    revision = source_revision(root)
    candidates = [prefix]
    if (root / "out/build" / PROFILE / "cmake/LudusSdkManifest.json").is_file():
        candidates.insert(0, prepared_sdk_prefix(root))
    for candidate in candidates:
        try:
            identity = validate_creation_sdk(candidate)
            if (identity.engine_version == version.group(1)
                    and revision and identity.source_revision == revision):
                return candidate
        except ToolingError:
            pass  # A missing/stale editor-owned install is prepared automatically.
    if prepare is None:
        raise ToolingError("MissingTools", "The editor engine needs preparation; use Create with prepared tooling")
    prefix = prepare(prefix) or prefix
    identity = validate_creation_sdk(prefix)
    if (identity.engine_version != version.group(1)
            or (revision and identity.source_revision != revision)):
        raise ToolingError("SdkIncompatible", "Prepared engine does not match the editor tooling checkout")
    return prefix


def prepare_creation_sdk(root: Path, prefix: Path, *, runner, cancel_check=None):
    """Shared explicit preparation; no system installs or project hooks."""
    from .project_setup import environment
    env = environment(root)
    env["CI"] = "true"
    commands = ([str(root / "scripts/init"), PROFILE, "--preset-only", "--cli", "--no-system-install"],
                [str(root / "scripts/build"), PROFILE])
    for argv in commands:
        if cancel_check:
            cancel_check()
        runner(argv, cwd=root, env=env)
    if cancel_check:
        cancel_check()
    # Configuration can change the variant, so choose the installation only
    # after building. Keep the old profile prefix intact, including legacy SDKs.
    prefix = prepared_sdk_prefix(root)
    runner([str(root / "out/host-tools/venv/bin/cmake"), "--install",
            str(root / "out/build" / PROFILE), "--prefix", str(prefix)], cwd=root, env=env)
    if cancel_check:
        cancel_check()
    return prefix
