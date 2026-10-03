"""Bounded release configuration shared by installed project operations."""
from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from .descriptor import TARGET_NAME_RE
from .errors import ToolingError

MAX_JSON_BYTES = 256 * 1024
IDENTIFIER = re.compile(r"[a-z0-9][a-z0-9-]{0,63}")
ITCH_TARGET = re.compile(r"[a-z0-9][a-z0-9_-]*/[a-z0-9][a-z0-9_-]*")
DIGEST = re.compile(r"[a-f0-9]{64}")


def fail(message: str, code: str = "InvalidRelease") -> None:
    raise ToolingError(code, message)


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail(f"duplicate JSON field {key!r}")
        result[key] = value
    return result


def json_bytes(data: bytes, context: str, *, maximum: int = MAX_JSON_BYTES) -> dict:
    if len(data) > maximum:
        fail(f"{context} exceeds {maximum} bytes")
    try:
        obj = json.loads(data.decode("utf-8"), object_pairs_hook=_object,
                         parse_constant=lambda _: fail("non-finite JSON number"))
    except (ValueError, UnicodeError, RecursionError) as exc:
        fail(f"invalid JSON in {context}: {exc}")
    if not isinstance(obj, dict):
        fail(f"{context} must be an object")
    return obj


def read_json(path: Path) -> dict:
    try:
        with path.open("rb") as handle:
            return json_bytes(handle.read(MAX_JSON_BYTES + 1), path.name)
    except OSError as exc:
        fail(f"cannot read {path.name}: {exc}")


def fields(obj, required: set[str], optional: set[str], context: str) -> None:
    if not isinstance(obj, dict) or not required <= obj.keys() or obj.keys() - required - optional:
        fail(f"{context} has missing or unknown fields")


def string(value, context: str, maximum: int = 240) -> str:
    if not isinstance(value, str) or not value or len(value) > maximum:
        fail(f"{context} must be a nonempty string of at most {maximum} characters")
    if any(ord(c) < 32 or ord(c) == 127 for c in value):
        fail(f"{context} contains control characters")
    return value


def payload_path(value) -> str:
    value = string(value, "payload path")
    path = PurePosixPath(value)
    if (path.is_absolute() or "\\" in value or ":" in value
            or any(part in ("", ".", "..") for part in value.split("/"))):
        fail(f"invalid portable payload path {value!r}", "InvalidPackage")
    return value


def identifier(value, context: str) -> str:
    if not IDENTIFIER.fullmatch(string(value, context, 64)):
        fail(f"invalid {context}")
    return value


def canonical(obj: dict) -> bytes:
    return (json.dumps(obj, sort_keys=True, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@dataclass(frozen=True)
class ReleaseProfile:
    target_platform: str
    build_profile: str
    target: str
    install_component: str
    entry_point: str


@dataclass(frozen=True)
class ReleaseConfig:
    profiles: dict[str, ReleaseProfile]
    itch_target: str | None
    destinations: dict[str, tuple[str, str]]
    digest: str


def load_release(project_dir: Path) -> ReleaseConfig:
    path = project_dir / "ludus.release.json"
    try:
        with path.open("rb") as handle:
            data = handle.read(MAX_JSON_BYTES + 1)
    except OSError as exc:
        fail(f"cannot read ludus.release.json: {exc}")
    obj = json_bytes(data, path.name)
    fields(obj, {"schemaVersion", "profiles"}, {"itch", "automation"}, "release")
    if type(obj["schemaVersion"]) is not int or obj["schemaVersion"] != 1:
        fail("unsupported release schemaVersion", "UnsupportedVersion")
    raw = obj["profiles"]
    if not isinstance(raw, dict) or not 1 <= len(raw) <= 32:
        fail("profiles must contain between 1 and 32 entries")
    profiles = {}
    for name, profile in raw.items():
        identifier(name, "profile name")
        fields(profile, {"targetPlatform", "buildProfile", "target", "installComponent", "entryPoint"},
               set(), f"profiles.{name}")
        if profile["targetPlatform"] != "linux-x64" or profile["buildProfile"] != "release":
            fail("only linux-x64 Release packaging is supported", "UnsupportedReleaseTarget")
        target = string(profile["target"], "target", 256)
        component = string(profile["installComponent"], "installComponent", 64)
        if not TARGET_NAME_RE.fullmatch(target) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]*", component):
            fail("invalid target or installComponent")
        profiles[name] = ReleaseProfile("linux-x64", "release", target, component,
                                        payload_path(profile["entryPoint"]))
    itch_target, destinations = None, {}
    if "itch" in obj:
        itch = obj["itch"]
        fields(itch, {"target", "channels"}, set(), "itch")
        itch_target = string(itch["target"], "itch.target", 128)
        if not ITCH_TARGET.fullmatch(itch_target):
            fail("itch.target must be lower-case username/game")
        if not isinstance(itch["channels"], dict) or not 1 <= len(itch["channels"]) <= 32:
            fail("itch.channels must contain between 1 and 32 entries")
        channels = set()
        for name, mapping in itch["channels"].items():
            identifier(name, "destination")
            fields(mapping, {"packageProfile", "channel"}, set(), f"itch.channels.{name}")
            profile = identifier(mapping["packageProfile"], "packageProfile")
            channel = identifier(mapping["channel"], "channel")
            if profile not in profiles or channel in channels:
                fail("channel references an unknown profile or duplicates a destination channel")
            channels.add(channel)
            destinations[name] = (profile, channel)
    if "automation" in obj:
        fields(obj["automation"], {"provider", "releaseTags"}, set(), "automation")
        if (obj["automation"]["provider"] != "github-actions"
                or type(obj["automation"]["releaseTags"]) is not bool):
            fail("invalid automation provider or releaseTags")
    return ReleaseConfig(profiles, itch_target, destinations, sha256(data))
