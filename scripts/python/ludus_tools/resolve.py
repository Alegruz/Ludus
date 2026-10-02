"""SDK resolution and resolved-input stamps (P05/P06/P10).

Resolution precedence for a version-2 project operation is, in order:

1. an explicit per-operation local SDK prefix (``--sdk`` on this command),
2. the saved local override in ``.ludus/local.json`` for the target/flavor,
3. the exact locked release SDK in the shared store.

The resolved identity, path and override status are always reported. A missing
SDK stops with an install hint; configure/build/run never download, refresh the
lock or compile engine sources. A mutable local prefix is fingerprinted by a
*resolved-input stamp* (manifest + header/library digests), because path
equality alone cannot detect that a developer rebuilt the engine in place —
changing the stamp forces reconfigure/relink and a changed stamp observed during
an operation aborts rather than mixing inputs.
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from .errors import SDK_NOT_FOUND, STAMP_CHANGED, UNRESOLVED_LOCK, ToolingError
from .identity import SdkIdentity, load_prefix_manifest, require_compatible
from .lockfile import Lock, LocalSettings
from .sdkstore import SdkStore

# How the SDK was selected, for display/diagnostics.
SOURCE_OPTION = "command-line --sdk override"
SOURCE_LOCAL = "saved .ludus/local.json override"
SOURCE_LOCKED = "locked release in the shared store"


@dataclass
class Resolution:
    prefix: Path
    identity: SdkIdentity
    source: str
    is_override: bool
    stamp: str

    def describe(self) -> str:
        rev = self.identity.source_revision or "unknown"
        override = " [OVERRIDE]" if self.is_override else ""
        return (
            f"SDK {self.identity.flavor} rev={rev} variant={self.identity.sdk_variant}\n"
            f"  path:   {self.prefix}\n"
            f"  source: {self.source}{override}"
        )


def resolve_sdk(
    *,
    target: str,
    flavor: str,
    lock: Lock,
    local_settings: LocalSettings,
    store: SdkStore,
    cli_prefix: Optional[Path] = None,
    required_features: Optional[list[str]] = None,
) -> Resolution:
    """Resolve the SDK for one operation following the documented precedence."""
    # 1) explicit per-operation override
    if cli_prefix is not None:
        prefix = Path(cli_prefix)
        identity = load_prefix_manifest(prefix)
        require_compatible(identity, required_flavor=flavor, required_features=required_features)
        return Resolution(prefix, identity, SOURCE_OPTION, True, compute_stamp(prefix))

    # 2) saved local override
    override = local_settings.find(target, flavor)
    if override is not None:
        prefix = Path(override.prefix)
        identity = load_prefix_manifest(prefix)
        require_compatible(identity, required_flavor=flavor, required_features=required_features)
        return Resolution(prefix, identity, SOURCE_LOCAL, True, compute_stamp(prefix))

    # 3) locked release in the store
    if not lock.resolved:
        raise ToolingError(
            UNRESOLVED_LOCK,
            "project lock is unresolved (no published release yet); select a local SDK with "
            "'ludus project engine --sdk <prefix>'. The project is not reproducible from a "
            "release until a release lock exists.",
        )
    entry = next((p for p in lock.packages if p.target == target and p.flavor == flavor), None)
    if entry is None:
        raise ToolingError(
            SDK_NOT_FOUND,
            f"lock has no package for target={target} flavor={flavor}; "
            "install the missing flavor or add an override",
        )
    installed = store.find_by_digest(entry.digest)
    if installed is None:
        raise ToolingError(
            SDK_NOT_FOUND,
            f"locked SDK (digest {entry.digest[:16]}…) is not installed; run "
            f"'ludus sdk install --version {lock.engine_version}'",
        )
    require_compatible(installed.identity, required_flavor=flavor, required_features=required_features)
    return Resolution(installed.prefix, installed.identity, SOURCE_LOCKED, False, compute_stamp(installed.prefix))


# --- Resolved-input stamp -----------------------------------------------------

_STAMP_FILES = (
    "share/Ludus/LudusSdkManifest.json",
    "lib/cmake/Ludus/LudusConfig.cmake",
    "lib/cmake/Ludus/LudusTargets.cmake",
)


def compute_stamp(prefix: Path) -> str:
    """A content fingerprint of the SDK inputs that affect a build.

    Hashes the manifest and exported CMake metadata plus the size+mtime of every
    installed static library. A locally rebuilt engine changes these even when
    the prefix path is unchanged, so the stamp — not path equality — gates
    reconfigure/relink for mutable local prefixes.
    """
    h = hashlib.sha256()
    prefix = Path(prefix)
    for rel in _STAMP_FILES:
        p = prefix / rel
        if p.is_file():
            h.update(rel.encode("utf-8"))
            h.update(p.read_bytes())
    lib_dir = prefix / "lib"
    if lib_dir.is_dir():
        for lib in sorted(lib_dir.glob("*.a")):
            st = lib.stat()
            h.update(f"{lib.name}:{st.st_size}:{int(st.st_mtime)}".encode("utf-8"))
    return h.hexdigest()


def stamp_path(build_dir: Path) -> Path:
    return Path(build_dir) / ".cmake" / ".ludus-sdk-stamp.json"


def read_stamp(build_dir: Path) -> Optional[str]:
    p = stamp_path(build_dir)
    if not p.is_file():
        return None
    try:
        return json.loads(p.read_text()).get("stamp")
    except (ValueError, OSError):
        return None


def write_stamp(build_dir: Path, resolution: Resolution) -> None:
    p = stamp_path(build_dir)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(
        json.dumps(
            {
                "stamp": resolution.stamp,
                "prefix": str(resolution.prefix),
                "sdk_variant": resolution.identity.sdk_variant,
                "source": resolution.source,
            },
            indent=2,
        )
        + "\n"
    )


def needs_reconfigure(build_dir: Path, resolution: Resolution) -> bool:
    """True when the SDK inputs changed since the last configure of this tree."""
    return read_stamp(build_dir) != resolution.stamp


def assert_stamp_unchanged(build_dir: Path, resolution: Resolution) -> None:
    """Fail if the SDK prefix content changed during an operation (P10)."""
    current = compute_stamp(resolution.prefix)
    if current != resolution.stamp:
        raise ToolingError(
            STAMP_CHANGED,
            "the resolved SDK changed on disk during this operation; aborting to avoid mixing "
            "inputs. Reconfigure and rebuild.",
        )
