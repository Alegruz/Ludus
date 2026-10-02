"""Version-1 Ludus editor project descriptor parsing and validation.

This is the Python half of the shared descriptor contract. It MUST accept and
reject exactly what the C++ ProjectStore does; both consume the same fixtures
under apps/editor/tests/fixtures so validation cannot drift
(.kiro/specs/editor-workspace/design.md section 4). The adapter revalidates all
fields here before executing any tool, independent of the UI.

Pure validation only: no I/O side effects beyond reading the given bytes, no
command execution, no path resolution against the editor launch directory.
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path


# Documented size limits (design section 4). Kept in one place so the C++ and
# Python validators share the exact bounds.
MAX_FILE_BYTES = 64 * 1024
MAX_NAME_BYTES = 128
MAX_PATH_BYTES = 4096
MAX_TARGET_BYTES = 256
MAX_ARG_COUNT = 64
MAX_ARG_BYTES = 4096
MAX_ARGS_TOTAL_BYTES = 32 * 1024

PRESETS = ("linux-clang-debug", "linux-clang-development")
PROVIDERS = ("ludus", "cmake")
TARGET_NAME_RE = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.+-]*")


class ProjectError(Exception):
    """A descriptor validation failure with a stable result code and message.

    This Python exception never crosses into C++: it is caught at the adapter
    boundary and translated to a protocol result (design section 8).
    """

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


@dataclass
class Descriptor:
    name: str
    provider: str
    source_dir: str
    preset: str
    target: str
    run_cwd: str
    run_args: list[str] = field(default_factory=list)


def _utf8_len(value: str) -> int:
    return len(value.encode("utf-8"))


def _is_relative(value: str) -> bool:
    # A relative path with no root; intentional ".." is allowed (resolved later),
    # absolute paths are rejected here.
    if not value:
        return False
    return not Path(value).is_absolute()


def _require_string(obj: dict, key: str, context: str) -> str:
    if key not in obj:
        raise ProjectError("InvalidProject", f"missing field '{context}{key}'")
    value = obj[key]
    if not isinstance(value, str):
        raise ProjectError("InvalidProject", f"'{context}{key}' must be a string")
    if "\x00" in value:
        raise ProjectError("InvalidProject", f"'{context}{key}' must not contain NUL")
    return value


def _reject_unknown(obj: dict, allowed: tuple[str, ...], context: str) -> None:
    for key in obj:
        if key not in allowed:
            raise ProjectError("InvalidProject", f"unknown field '{context}{key}'")


def parse_descriptor_bytes(data: bytes) -> Descriptor:
    """Validate raw descriptor bytes and return a Descriptor, or raise ProjectError."""
    if len(data) > MAX_FILE_BYTES:
        raise ProjectError("InvalidProject", "descriptor exceeds the 64 KiB size limit")

    # Reject invalid UTF-8 / unpaired surrogates / embedded NUL before JSON.
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ProjectError("InvalidProject", "descriptor is not valid UTF-8") from exc
    try:
        text.encode("utf-8", "strict")
    except UnicodeEncodeError as exc:
        raise ProjectError("InvalidProject", "descriptor contains unpaired surrogates") from exc
    if "\x00" in text:
        raise ProjectError("InvalidProject", "descriptor contains a NUL byte")

    try:
        root = json.loads(text)
    except ValueError as exc:
        raise ProjectError("InvalidProject", f"malformed JSON: {exc}") from exc
    if not isinstance(root, dict):
        raise ProjectError("InvalidProject", "descriptor must be a JSON object")

    _reject_unknown(root, ("version", "name", "provider", "source_dir", "preset", "target", "run"), "")

    # version: a JSON number numerically equal to 1; reject bool/string/float.
    if "version" not in root:
        raise ProjectError("InvalidProject", "missing field 'version'")
    version = root["version"]
    if isinstance(version, bool) or not isinstance(version, (int, float)):
        raise ProjectError("UnsupportedVersion", "'version' must be the number 1")
    if isinstance(version, float) and not version.is_integer():
        raise ProjectError("UnsupportedVersion", "'version' must be an integer equal to 1")
    if int(version) != 1:
        raise ProjectError("UnsupportedVersion", "unsupported descriptor version; only version 1 is supported")

    name = _require_string(root, "name", "")
    if not name:
        raise ProjectError("InvalidProject", "'name' must be nonempty")
    if _utf8_len(name) > MAX_NAME_BYTES:
        raise ProjectError("InvalidProject", "'name' exceeds 128 UTF-8 bytes")

    provider = _require_string(root, "provider", "")
    if provider not in PROVIDERS:
        raise ProjectError("InvalidProject", "'provider' must be exactly 'ludus' or 'cmake'")

    source_dir = _require_string(root, "source_dir", "")
    if not _is_relative(source_dir) or _utf8_len(source_dir) > MAX_PATH_BYTES:
        raise ProjectError("InvalidProject", "'source_dir' must be a relative path within 4096 UTF-8 bytes")

    preset = _require_string(root, "preset", "")
    if preset not in PRESETS:
        raise ProjectError("InvalidProject", "'preset' must be linux-clang-debug or linux-clang-development")

    target = _require_string(root, "target", "")
    if not TARGET_NAME_RE.fullmatch(target) or _utf8_len(target) > MAX_TARGET_BYTES:
        raise ProjectError("InvalidProject", "'target' must match [A-Za-z0-9_][A-Za-z0-9_.+-]* within 256 bytes")

    if "run" not in root:
        raise ProjectError("InvalidProject", "missing field 'run'")
    run = root["run"]
    if not isinstance(run, dict):
        raise ProjectError("InvalidProject", "'run' must be an object")
    _reject_unknown(run, ("cwd", "args"), "run.")

    run_cwd = _require_string(run, "cwd", "run.")
    if not _is_relative(run_cwd) or _utf8_len(run_cwd) > MAX_PATH_BYTES:
        raise ProjectError("InvalidProject", "'run.cwd' must be a relative path within 4096 UTF-8 bytes")

    if "args" not in run:
        raise ProjectError("InvalidProject", "missing field 'run.args'")
    args = run["args"]
    if not isinstance(args, list):
        raise ProjectError("InvalidProject", "'run.args' must be an array")
    if len(args) > MAX_ARG_COUNT:
        raise ProjectError("InvalidProject", "'run.args' exceeds 64 entries")
    total = 0
    run_args: list[str] = []
    for item in args:
        if not isinstance(item, str):
            raise ProjectError("InvalidProject", "every 'run.args' entry must be a string")
        if "\x00" in item:
            raise ProjectError("InvalidProject", "'run.args' entries must not contain NUL")
        length = _utf8_len(item)
        if length > MAX_ARG_BYTES:
            raise ProjectError("InvalidProject", "a 'run.args' entry exceeds 4096 UTF-8 bytes")
        total += length
        run_args.append(item)  # empty strings are valid
    if total > MAX_ARGS_TOTAL_BYTES:
        raise ProjectError("InvalidProject", "'run.args' total exceeds 32 KiB")

    return Descriptor(
        name=name,
        provider=provider,
        source_dir=source_dir,
        preset=preset,
        target=target,
        run_cwd=run_cwd,
        run_args=run_args,
    )


def parse_descriptor_file(path: Path) -> Descriptor:
    """Read and validate a descriptor file (bounded)."""
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise ProjectError("InvalidProject", f"cannot read descriptor: {exc}") from exc
    return parse_descriptor_bytes(data)
