"""Versioned project lock (``ludus.lock.json``) and ignored local settings.

The lock records the *exact* engine version/revision and per-target/flavor
package identities and checksums, plus the template version. It never contains an
installation path (P04). Ordinary build never writes it; explicit engine
selection / lock update does. Descriptor-lock agreement is validated before use.

When no published release exists yet, the lock is in an explicit *unresolved*
state (``"resolved": false`` with no fabricated package entries). A build then
requires a local SDK override and diagnostics state the project is not
reproducible from a release (design "Project files and schema evolution").

``.ludus/local.json`` holds ignored, machine-local SDK overrides keyed by
target/flavor. Changing the override never rewrites the committed lock.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from .errors import INVALID_PROJECT, LOCK_MISMATCH, UNSUPPORTED_VERSION, ToolingError

LOCK_SCHEMA_VERSION = 1
LOCAL_SCHEMA_VERSION = 1
MAX_LOCK_BYTES = 256 * 1024

# The three native flavors locked together so a profile change does not change
# dependency intent (design: "locking all three native flavors").
NATIVE_FLAVORS = ("Debug", "Development", "Release")


@dataclass
class PackageEntry:
    """One resolved SDK package for a (target, flavor)."""

    target: str
    flavor: str
    sdk_variant: str
    digest: str  # sha256 hex of the payload archive
    size: int

    def to_json(self) -> dict:
        return {
            "target": self.target,
            "flavor": self.flavor,
            "sdk_variant": self.sdk_variant,
            "digest": self.digest,
            "size": self.size,
        }

    @staticmethod
    def from_json(obj: dict) -> "PackageEntry":
        return PackageEntry(
            target=str(obj["target"]),
            flavor=str(obj["flavor"]),
            sdk_variant=str(obj["sdk_variant"]),
            digest=str(obj["digest"]),
            size=int(obj["size"]),
        )


@dataclass
class Lock:
    schema_version: int = LOCK_SCHEMA_VERSION
    resolved: bool = False
    engine_version: str = ""
    engine_revision: str = ""
    template_version: int = 0
    packages: list[PackageEntry] = field(default_factory=list)

    def to_json(self) -> dict:
        return {
            "schema_version": self.schema_version,
            "resolved": self.resolved,
            "engine": {
                "version": self.engine_version,
                "revision": self.engine_revision,
            },
            "template_version": self.template_version,
            "packages": [p.to_json() for p in self.packages],
        }

    def serialize(self) -> bytes:
        return (json.dumps(self.to_json(), indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def unresolved_lock(engine_version: str, template_version: int) -> Lock:
    """A lock with no release packages yet; the game needs a local override."""
    return Lock(
        resolved=False,
        engine_version=engine_version,
        engine_revision="",
        template_version=template_version,
        packages=[],
    )


def parse_lock_bytes(data: bytes) -> Lock:
    if len(data) > MAX_LOCK_BYTES:
        raise ToolingError(INVALID_PROJECT, "lock exceeds size bound")
    try:
        obj = json.loads(data.decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as exc:
        raise ToolingError(INVALID_PROJECT, f"malformed lock: {exc}") from exc
    if not isinstance(obj, dict):
        raise ToolingError(INVALID_PROJECT, "lock must be a JSON object")
    schema = obj.get("schema_version")
    if schema != LOCK_SCHEMA_VERSION:
        raise ToolingError(UNSUPPORTED_VERSION, f"unsupported lock schema {schema!r}")
    resolved = obj.get("resolved")
    if not isinstance(resolved, bool):
        raise ToolingError(INVALID_PROJECT, "lock 'resolved' must be a boolean")
    engine = obj.get("engine", {})
    if not isinstance(engine, dict):
        raise ToolingError(INVALID_PROJECT, "lock 'engine' must be an object")
    packages_raw = obj.get("packages", [])
    if not isinstance(packages_raw, list):
        raise ToolingError(INVALID_PROJECT, "lock 'packages' must be an array")
    packages = [PackageEntry.from_json(p) for p in packages_raw]
    if resolved and not packages:
        raise ToolingError(INVALID_PROJECT, "a resolved lock must list packages")
    if not resolved and packages:
        raise ToolingError(INVALID_PROJECT, "an unresolved lock must not list packages")
    return Lock(
        schema_version=LOCK_SCHEMA_VERSION,
        resolved=resolved,
        engine_version=str(engine.get("version", "")),
        engine_revision=str(engine.get("revision", "")),
        template_version=int(obj.get("template_version", 0)),
        packages=packages,
    )


def parse_lock_file(path: Path) -> Lock:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise ToolingError(INVALID_PROJECT, f"cannot read lock: {exc}") from exc
    return parse_lock_bytes(data)


def validate_descriptor_lock_agreement(descriptor_engine_version: str, lock: Lock) -> None:
    """The committed descriptor's engine version must match the lock's."""
    if lock.engine_version != descriptor_engine_version:
        raise ToolingError(
            LOCK_MISMATCH,
            "descriptor engine.version "
            f"{descriptor_engine_version!r} disagrees with lock {lock.engine_version!r}",
        )


# --- Ignored local settings (.ludus/local.json) ------------------------------


@dataclass
class LocalOverride:
    target: str
    flavor: str
    prefix: str  # absolute path to a locally installed SDK prefix


@dataclass
class LocalSettings:
    schema_version: int = LOCAL_SCHEMA_VERSION
    overrides: list[LocalOverride] = field(default_factory=list)

    def find(self, target: str, flavor: str) -> Optional[LocalOverride]:
        for o in self.overrides:
            if o.target == target and o.flavor == flavor:
                return o
        return None

    def set_override(self, target: str, flavor: str, prefix: str) -> None:
        self.overrides = [o for o in self.overrides if not (o.target == target and o.flavor == flavor)]
        self.overrides.append(LocalOverride(target=target, flavor=flavor, prefix=prefix))

    def clear_override(self, target: str, flavor: str) -> bool:
        before = len(self.overrides)
        self.overrides = [o for o in self.overrides if not (o.target == target and o.flavor == flavor)]
        return len(self.overrides) != before

    def to_json(self) -> dict:
        return {
            "schema_version": self.schema_version,
            "sdk_overrides": [
                {"target": o.target, "flavor": o.flavor, "prefix": o.prefix} for o in self.overrides
            ],
        }

    def serialize(self) -> bytes:
        return (json.dumps(self.to_json(), indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def parse_local_settings_bytes(data: bytes) -> LocalSettings:
    if len(data) > MAX_LOCK_BYTES:
        raise ToolingError(INVALID_PROJECT, "local settings exceed size bound")
    try:
        obj = json.loads(data.decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as exc:
        raise ToolingError(INVALID_PROJECT, f"malformed local settings: {exc}") from exc
    if not isinstance(obj, dict):
        raise ToolingError(INVALID_PROJECT, "local settings must be a JSON object")
    if obj.get("schema_version") != LOCAL_SCHEMA_VERSION:
        raise ToolingError(UNSUPPORTED_VERSION, "unsupported local settings schema")
    overrides: list[LocalOverride] = []
    for entry in obj.get("sdk_overrides", []):
        if not isinstance(entry, dict):
            raise ToolingError(INVALID_PROJECT, "each sdk override must be an object")
        overrides.append(
            LocalOverride(
                target=str(entry["target"]),
                flavor=str(entry["flavor"]),
                prefix=str(entry["prefix"]),
            )
        )
    return LocalSettings(schema_version=LOCAL_SCHEMA_VERSION, overrides=overrides)


def parse_local_settings_file(path: Path) -> LocalSettings:
    if not path.is_file():
        return LocalSettings()
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise ToolingError(INVALID_PROJECT, f"cannot read local settings: {exc}") from exc
    return parse_local_settings_bytes(data)
