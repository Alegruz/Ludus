"""Build-generation publication and leasing for project-live-reload (design 5).

A generation is an immutable directory containing a validated gameplay module
copy, its matching debug symbols, and a bounded manifest. Publication is atomic:
staging copies and verifies into a private temp directory, then a single
rename moves it to a unique, never-reused generation id. Failed/cancelled builds
never activate a stale artifact. Active and debugger-leased generations are
never overwritten or collected; retention keeps active + candidate + previous.

This module is Qt-free and import-safe for tests. The editor supervisor and the
Python play-session adapter call publish_generation() after a verified build and
resolve artifacts through the shared CMake File API resolver (cmake_targets),
never by guessing output paths.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

# Manifest bounds (design 5): manifest <= 64 KiB, at most 16 payload files; v1
# payload is module + symbols, not a dynamic plugin graph.
MAX_MANIFEST_BYTES = 64 * 1024
MAX_PAYLOAD_FILES = 16
MANIFEST_SCHEMA = 1

# Default retention: active, candidate and previous generation (design 5).
DEFAULT_RETENTION = 3


class PublishError(Exception):
    """A build generation could not be published; the old active stays current."""


@dataclass
class GenerationManifest:
    """The bounded metadata recorded alongside a published generation."""

    schema: int
    generation_id: str
    project_id: str
    game_id: str
    module_target: str
    host_target: str
    module_file: str
    symbol_file: str | None
    sdk_identity: str
    build_request_revision: str
    source_input_digest: str
    abi_major: int
    abi_minor: int
    property_schema: int
    checkpoint_schema: int
    files: dict[str, str] = field(default_factory=dict)  # relative path -> sha256
    created_unix: int = 0

    def to_json(self) -> str:
        return json.dumps(self.__dict__, ensure_ascii=False, separators=(",", ":"), sort_keys=True)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_input_digest(source_files: list[Path]) -> str:
    """Digest of declared source inputs for the superseded-build check (design 5).

    Conservative, not a hermetic snapshot: hashes the sorted (relpath, content)
    of the declared inputs so a change between build and publication is visible.
    """
    digest = hashlib.sha256()
    for path in sorted(source_files, key=lambda p: str(p)):
        try:
            digest.update(str(path).encode("utf-8"))
            digest.update(b"\0")
            digest.update(sha256_file(path).encode("ascii"))
            digest.update(b"\0")
        except OSError:
            digest.update(b"<missing>\0")
    return digest.hexdigest()


_GEN_COUNTER = 0


def next_generation_id() -> str:
    """An opaque, strictly-monotonic, never-reused generation id.

    The leading millisecond timestamp plus a process-wide monotonic counter
    guarantees lexical sort order equals publish order (so retention keeps the
    actually-newest generations), and the random suffix keeps ids unguessable.
    """
    global _GEN_COUNTER
    _GEN_COUNTER += 1
    return (
        f"{int(time.time() * 1000):013d}-{_GEN_COUNTER:09d}-"
        f"{os.getpid():06d}-{int.from_bytes(os.urandom(3), 'big'):06d}"
    )


def publish_generation(
    *,
    generations_root: Path,
    module_artifact: Path,
    host_artifact: Path,
    symbol_artifact: Path | None,
    manifest_fields: dict[str, Any],
    declared_source_inputs: list[Path],
    pre_build_source_digest: str | None = None,
) -> Path:
    """Atomically publish one immutable generation; return its directory.

    Staging copies the module (+symbols) into a private temp dir under the
    generations root, verifies hashes, writes the bounded manifest, then renames
    the staging dir onto a unique generation id. If the declared source inputs
    changed since `pre_build_source_digest` the result is Superseded and nothing
    is published (design 5: never label a changed-input result the latest).
    """
    generations_root.mkdir(parents=True, exist_ok=True)

    if not module_artifact.is_file():
        raise PublishError(f"module artifact missing: {module_artifact}")
    if not host_artifact.is_file():
        raise PublishError(f"host artifact missing: {host_artifact}")

    current_digest = source_input_digest(declared_source_inputs)
    if pre_build_source_digest is not None and current_digest != pre_build_source_digest:
        raise PublishError("source inputs changed during build; result is Superseded")

    gen_id = next_generation_id()
    # Stage into a private temp dir in the SAME filesystem so the publish is an
    # atomic rename (never a cross-device copy that could be half-written).
    staging = Path(tempfile.mkdtemp(prefix=f".staging-{gen_id}-", dir=generations_root))
    try:
        payload_files: dict[str, str] = {}
        module_name = module_artifact.name
        shutil.copy2(module_artifact, staging / module_name)
        payload_files[module_name] = sha256_file(staging / module_name)

        symbol_name: str | None = None
        if symbol_artifact is not None and symbol_artifact.is_file():
            symbol_name = symbol_artifact.name
            shutil.copy2(symbol_artifact, staging / symbol_name)
            payload_files[symbol_name] = sha256_file(staging / symbol_name)

        if len(payload_files) > MAX_PAYLOAD_FILES:
            raise PublishError(f"too many payload files ({len(payload_files)} > {MAX_PAYLOAD_FILES})")

        # Verify the staged copy byte-for-byte matches the source artifact.
        if payload_files[module_name] != sha256_file(module_artifact):
            raise PublishError("module copy hash mismatch; corrupt staging")

        manifest = GenerationManifest(
            schema=MANIFEST_SCHEMA,
            generation_id=gen_id,
            project_id=str(manifest_fields.get("project_id", "")),
            game_id=str(manifest_fields.get("game_id", "")),
            module_target=str(manifest_fields.get("module_target", "")),
            host_target=str(manifest_fields.get("host_target", "")),
            module_file=module_name,
            symbol_file=symbol_name,
            sdk_identity=str(manifest_fields.get("sdk_identity", "")),
            build_request_revision=str(manifest_fields.get("build_request_revision", "")),
            source_input_digest=current_digest,
            abi_major=int(manifest_fields.get("abi_major", 1)),
            abi_minor=int(manifest_fields.get("abi_minor", 0)),
            property_schema=int(manifest_fields.get("property_schema", 0)),
            checkpoint_schema=int(manifest_fields.get("checkpoint_schema", 0)),
            files=payload_files,
            created_unix=int(time.time()),
        )
        manifest_text = manifest.to_json()
        if len(manifest_text.encode("utf-8")) > MAX_MANIFEST_BYTES:
            raise PublishError("manifest exceeds the 64 KiB bound")
        (staging / "manifest.json").write_text(manifest_text, encoding="utf-8")

        final = generations_root / gen_id
        if final.exists():
            raise PublishError(f"generation id already exists (never reuse): {gen_id}")
        os.rename(staging, final)
        # Make the published generation read-only so an active/leased image is
        # not overwritten in place.
        for child in final.iterdir():
            os.chmod(child, 0o444)
        return final
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise


def read_manifest(generation_dir: Path) -> dict[str, Any]:
    manifest_path = generation_dir / "manifest.json"
    size = manifest_path.stat().st_size
    if size > MAX_MANIFEST_BYTES:
        raise PublishError(f"manifest exceeds the 64 KiB bound: {size}")
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    # Re-verify payload hashes before a generation is trusted for load.
    for rel, expected in data.get("files", {}).items():
        actual = sha256_file(generation_dir / rel)
        if actual != expected:
            raise PublishError(f"payload hash mismatch for {rel}; corrupt generation")
    return data


class LeaseSet:
    """Tracks which generations are leased (active/candidate/debugger).

    GC never deletes a leased generation and never deletes the active/candidate/
    previous within the retention window. A crashed session's lease is only
    reclaimable after its process is proven ended (checked by the caller).
    """

    def __init__(self) -> None:
        self._leases: set[str] = set()

    def acquire(self, generation_id: str) -> None:
        self._leases.add(generation_id)

    def release(self, generation_id: str) -> None:
        self._leases.discard(generation_id)

    def is_leased(self, generation_id: str) -> bool:
        return generation_id in self._leases


def collect_generations(
    generations_root: Path,
    leases: LeaseSet,
    *,
    retention: int = DEFAULT_RETENTION,
) -> list[str]:
    """Delete old generations beyond retention that are not leased.

    Returns the list of deleted generation ids. Never deletes a leased
    generation. Keeps the newest `retention` generations (active + candidate +
    previous by default). Retirement failure (a still-leased old generation) is
    surfaced by the caller as RestartRequired rather than growing unbounded.
    """
    if not generations_root.is_dir():
        return []
    gens = sorted(
        (p.name for p in generations_root.iterdir() if p.is_dir() and not p.name.startswith(".staging-")),
    )
    deleted: list[str] = []
    # Oldest first; keep the newest `retention`.
    collectible = gens[:-retention] if len(gens) > retention else []
    for gen_id in collectible:
        if leases.is_leased(gen_id):
            continue
        target = generations_root / gen_id
        for child in target.iterdir():
            os.chmod(child, 0o644)
        shutil.rmtree(target, ignore_errors=True)
        deleted.append(gen_id)
    return deleted
