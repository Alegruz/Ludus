"""Bounded version-1 and version-2 project descriptor parsing.

This is the shared descriptor contract for the independent-project workflow. It
preserves the version-1 fields exactly (``editor_project.py`` remains the legacy
reference and still parses v1), and adds version 2 with ``engine`` and
``template`` objects (design "Project files and schema evolution", P04/P09).

Rules, all enforced here and mirrored by shared fixtures:

* Version 1 keeps name/provider/source_dir/preset/target/run with identical
  limits and unknown-field rejection. Version-1 readers remain supported.
* Version 2 adds exactly ``engine`` and ``template`` objects. Only the external
  provider ``cmake`` carries an ``engine`` declaration; provider ``ludus`` is the
  engine-developer workflow and never acquires a downloaded engine dependency.
* All strings are bounded; unknown fields are rejected; the whole file is
  bounded to 64 KiB. Parsing is pure: no I/O beyond the bytes handed in.

Note: this module is intentionally standalone (no import of ``editor_project``)
so the installed tooling has no engine-checkout dependency, but the limits and
v1 verdicts are kept byte-for-byte identical and checked against the same
``apps/editor/tests/fixtures`` cases.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional

from .errors import INVALID_PROJECT, UNSUPPORTED_VERSION, ToolingError

# --- Shared size limits (must equal editor_project.py and the C++ limits) -----
MAX_FILE_BYTES = 64 * 1024
MAX_NAME_BYTES = 128
MAX_PATH_BYTES = 4096
MAX_TARGET_BYTES = 256
MAX_ARG_COUNT = 64
MAX_ARG_BYTES = 4096
MAX_ARGS_TOTAL_BYTES = 32 * 1024

# --- Version-2 additional limits ---------------------------------------------
MAX_VERSION_BYTES = 128
MAX_COMPONENT_COUNT = 64
MAX_COMPONENT_BYTES = 128
MAX_FEATURE_COUNT = 64
MAX_FEATURE_BYTES = 128
MAX_TEMPLATE_ID_BYTES = 128

# Presets accepted in a descriptor. Version 1 keeps the historical two; version 2
# native projects additionally allow the Release preset (design: extend E0 to
# Release). Legacy v1 descriptors continue to accept only the original two.
PRESETS_V1 = ("linux-clang-debug", "linux-clang-development")
from .native import PRESET_FLAVOR

PRESETS_V2 = tuple(PRESET_FLAVOR)
PROVIDERS = ("ludus", "cmake")

TARGET_NAME_RE = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.+-]*")
# Exact release version: a conservative semver-ish token, no ranges/wildcards.
ENGINE_VERSION_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.+-]*")
# Public module/component name, e.g. FoundationBase, GraphicsRhi.
COMPONENT_RE = re.compile(r"[A-Za-z][A-Za-z0-9]*")
# Feature flag token.
FEATURE_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*")
TEMPLATE_ID_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*")


@dataclass
class EngineRequirement:
    """The committed engine requirement (version-2, provider ``cmake`` only).

    ``version`` is an exact release version (not a floating range). ``components``
    lists requested public modules; ``features`` lists required feature flags.
    """

    version: str
    components: list[str] = field(default_factory=list)
    features: list[str] = field(default_factory=list)


@dataclass
class TemplateRef:
    """Identifies the generation inputs that produced the project."""

    id: str
    version: int


@dataclass
class Descriptor:
    version: int
    name: str
    provider: str
    source_dir: str
    preset: str
    target: str
    run_cwd: str
    run_args: list[str] = field(default_factory=list)
    engine: Optional[EngineRequirement] = None
    template: Optional[TemplateRef] = None


def _utf8_len(value: str) -> int:
    return len(value.encode("utf-8"))


def _is_relative(value: str) -> bool:
    if not value:
        return False
    return not Path(value).is_absolute()


def _err(code: str, message: str) -> ToolingError:
    return ToolingError(code, message)


def _require_string(obj: dict, key: str, context: str) -> str:
    if key not in obj:
        raise _err(INVALID_PROJECT, f"missing field '{context}{key}'")
    value = obj[key]
    if not isinstance(value, str):
        raise _err(INVALID_PROJECT, f"'{context}{key}' must be a string")
    if "\x00" in value:
        raise _err(INVALID_PROJECT, f"'{context}{key}' must not contain NUL")
    return value


def _reject_unknown(obj: dict, allowed: tuple[str, ...], context: str) -> None:
    for key in obj:
        if key not in allowed:
            raise _err(INVALID_PROJECT, f"unknown field '{context}{key}'")


def _bounded_string_list(
    values: Any,
    *,
    context: str,
    max_count: int,
    max_bytes: int,
    pattern: re.Pattern[str],
) -> list[str]:
    if not isinstance(values, list):
        raise _err(INVALID_PROJECT, f"'{context}' must be an array")
    if len(values) > max_count:
        raise _err(INVALID_PROJECT, f"'{context}' exceeds {max_count} entries")
    out: list[str] = []
    seen: set[str] = set()
    for item in values:
        if not isinstance(item, str):
            raise _err(INVALID_PROJECT, f"every '{context}' entry must be a string")
        if "\x00" in item:
            raise _err(INVALID_PROJECT, f"'{context}' entries must not contain NUL")
        if _utf8_len(item) > max_bytes or not pattern.fullmatch(item):
            raise _err(INVALID_PROJECT, f"invalid '{context}' entry: {item!r}")
        if item in seen:
            raise _err(INVALID_PROJECT, f"duplicate '{context}' entry: {item!r}")
        seen.add(item)
        out.append(item)
    return out


def _decode(data: bytes) -> dict:
    if len(data) > MAX_FILE_BYTES:
        raise _err(INVALID_PROJECT, "descriptor exceeds the 64 KiB size limit")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise _err(INVALID_PROJECT, "descriptor is not valid UTF-8") from exc
    try:
        text.encode("utf-8", "strict")
    except UnicodeEncodeError as exc:
        raise _err(INVALID_PROJECT, "descriptor contains unpaired surrogates") from exc
    if "\x00" in text:
        raise _err(INVALID_PROJECT, "descriptor contains a NUL byte")
    try:
        root = json.loads(text)
    except ValueError as exc:
        raise _err(INVALID_PROJECT, f"malformed JSON: {exc}") from exc
    if not isinstance(root, dict):
        raise _err(INVALID_PROJECT, "descriptor must be a JSON object")
    return root


def _read_version(root: dict) -> int:
    if "version" not in root:
        raise _err(INVALID_PROJECT, "missing field 'version'")
    version = root["version"]
    if isinstance(version, bool) or not isinstance(version, (int, float)):
        raise _err(UNSUPPORTED_VERSION, "'version' must be the number 1 or 2")
    if isinstance(version, float) and not version.is_integer():
        raise _err(UNSUPPORTED_VERSION, "'version' must be an integer")
    value = int(version)
    if value not in (1, 2):
        raise _err(
            UNSUPPORTED_VERSION,
            f"unsupported descriptor version {value}; this tooling supports 1 and 2",
        )
    return value


def _parse_common(root: dict, *, presets: tuple[str, ...]) -> dict:
    name = _require_string(root, "name", "")
    if not name:
        raise _err(INVALID_PROJECT, "'name' must be nonempty")
    if _utf8_len(name) > MAX_NAME_BYTES:
        raise _err(INVALID_PROJECT, "'name' exceeds 128 UTF-8 bytes")

    provider = _require_string(root, "provider", "")
    if provider not in PROVIDERS:
        raise _err(INVALID_PROJECT, "'provider' must be exactly 'ludus' or 'cmake'")

    source_dir = _require_string(root, "source_dir", "")
    if not _is_relative(source_dir) or _utf8_len(source_dir) > MAX_PATH_BYTES:
        raise _err(INVALID_PROJECT, "'source_dir' must be a relative path within 4096 UTF-8 bytes")

    preset = _require_string(root, "preset", "")
    if preset not in presets:
        raise _err(INVALID_PROJECT, f"'preset' must be one of {presets}")

    target = _require_string(root, "target", "")
    if not TARGET_NAME_RE.fullmatch(target) or _utf8_len(target) > MAX_TARGET_BYTES:
        raise _err(INVALID_PROJECT, "'target' must match [A-Za-z0-9_][A-Za-z0-9_.+-]* within 256 bytes")

    if "run" not in root:
        raise _err(INVALID_PROJECT, "missing field 'run'")
    run = root["run"]
    if not isinstance(run, dict):
        raise _err(INVALID_PROJECT, "'run' must be an object")
    _reject_unknown(run, ("cwd", "args"), "run.")

    run_cwd = _require_string(run, "cwd", "run.")
    if not _is_relative(run_cwd) or _utf8_len(run_cwd) > MAX_PATH_BYTES:
        raise _err(INVALID_PROJECT, "'run.cwd' must be a relative path within 4096 UTF-8 bytes")

    if "args" not in run:
        raise _err(INVALID_PROJECT, "missing field 'run.args'")
    args = run["args"]
    if not isinstance(args, list):
        raise _err(INVALID_PROJECT, "'run.args' must be an array")
    if len(args) > MAX_ARG_COUNT:
        raise _err(INVALID_PROJECT, "'run.args' exceeds 64 entries")
    total = 0
    run_args: list[str] = []
    for item in args:
        if not isinstance(item, str):
            raise _err(INVALID_PROJECT, "every 'run.args' entry must be a string")
        if "\x00" in item:
            raise _err(INVALID_PROJECT, "'run.args' entries must not contain NUL")
        length = _utf8_len(item)
        if length > MAX_ARG_BYTES:
            raise _err(INVALID_PROJECT, "a 'run.args' entry exceeds 4096 UTF-8 bytes")
        total += length
        run_args.append(item)
    if total > MAX_ARGS_TOTAL_BYTES:
        raise _err(INVALID_PROJECT, "'run.args' total exceeds 32 KiB")

    return {
        "name": name,
        "provider": provider,
        "source_dir": source_dir,
        "preset": preset,
        "target": target,
        "run_cwd": run_cwd,
        "run_args": run_args,
    }


def _parse_engine(obj: Any) -> EngineRequirement:
    if not isinstance(obj, dict):
        raise _err(INVALID_PROJECT, "'engine' must be an object")
    _reject_unknown(obj, ("version", "components", "features"), "engine.")
    version = _require_string(obj, "version", "engine.")
    if not version or _utf8_len(version) > MAX_VERSION_BYTES or not ENGINE_VERSION_RE.fullmatch(version):
        raise _err(INVALID_PROJECT, "'engine.version' must be an exact release token")
    components = _bounded_string_list(
        obj.get("components", []),
        context="engine.components",
        max_count=MAX_COMPONENT_COUNT,
        max_bytes=MAX_COMPONENT_BYTES,
        pattern=COMPONENT_RE,
    )
    features = _bounded_string_list(
        obj.get("features", []),
        context="engine.features",
        max_count=MAX_FEATURE_COUNT,
        max_bytes=MAX_FEATURE_BYTES,
        pattern=FEATURE_RE,
    )
    return EngineRequirement(version=version, components=components, features=features)


def _parse_template(obj: Any) -> TemplateRef:
    if not isinstance(obj, dict):
        raise _err(INVALID_PROJECT, "'template' must be an object")
    _reject_unknown(obj, ("id", "version"), "template.")
    template_id = _require_string(obj, "id", "template.")
    if not template_id or _utf8_len(template_id) > MAX_TEMPLATE_ID_BYTES or not TEMPLATE_ID_RE.fullmatch(template_id):
        raise _err(INVALID_PROJECT, "'template.id' must be a bounded identifier token")
    if "version" not in obj:
        raise _err(INVALID_PROJECT, "missing field 'template.version'")
    version = obj["version"]
    # Accept an integer-valued JSON number (``1`` or ``1.0``) to match the C++
    # twin and the top-level ``version`` handling; reject bools and non-integer
    # floats so the same bytes get the same verdict in both readers.
    if isinstance(version, bool) or not isinstance(version, (int, float)):
        raise _err(INVALID_PROJECT, "'template.version' must be a positive integer")
    if isinstance(version, float) and not version.is_integer():
        raise _err(INVALID_PROJECT, "'template.version' must be a positive integer")
    version = int(version)
    if version < 1 or version > 1_000_000:
        raise _err(INVALID_PROJECT, "'template.version' must be a positive integer")
    return TemplateRef(id=template_id, version=version)


def parse_descriptor_bytes(data: bytes) -> Descriptor:
    """Validate raw descriptor bytes (v1 or v2) and return a Descriptor."""
    root = _decode(data)
    version = _read_version(root)

    if version == 1:
        _reject_unknown(
            root,
            ("version", "name", "provider", "source_dir", "preset", "target", "run"),
            "",
        )
        common = _parse_common(root, presets=PRESETS_V1)
        return Descriptor(version=1, engine=None, template=None, **common)

    # version == 2
    _reject_unknown(
        root,
        ("version", "name", "provider", "source_dir", "preset", "target", "run", "engine", "template"),
        "",
    )
    common = _parse_common(root, presets=PRESETS_V2)

    engine: Optional[EngineRequirement] = None
    if common["provider"] == "cmake":
        if "engine" not in root:
            raise _err(INVALID_PROJECT, "version-2 'cmake' projects require an 'engine' object")
        engine = _parse_engine(root["engine"])
    else:  # provider == "ludus": engine-developer workflow, never a downloaded dep
        if "engine" in root:
            raise _err(
                INVALID_PROJECT,
                "provider 'ludus' must not declare an 'engine' requirement",
            )

    template: Optional[TemplateRef] = None
    if "template" in root:
        template = _parse_template(root["template"])

    return Descriptor(version=2, engine=engine, template=template, **common)


def parse_descriptor_file(path: Path) -> Descriptor:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise _err(INVALID_PROJECT, f"cannot read descriptor: {exc}") from exc
    return parse_descriptor_bytes(data)
