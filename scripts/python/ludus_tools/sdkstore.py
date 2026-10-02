"""Shared immutable SDK store: install, validate, resolve (P05/P06).

The store lives under an XDG user data directory (default
``~/.local/share/ludus``), overridable by ``--store`` / ``LUDUS_SDK_STORE`` for
CI and portable use. Each final SDK directory is addressed by its validated
identity + payload digest and is immutable; several projects reference the same
files.

Installation is deliberately strict and bounded:

* An SDK-specific cooperative lock serializes installers of the same identity.
* The archive is validated BEFORE extraction: compressed/expanded size and entry
  count bounds, rejection of path traversal, absolute names, escaping symlinks,
  and unsupported entry types.
* The payload digest and manifest identity are verified.
* Extraction goes to a sibling staging directory on the same filesystem and is
  then atomically published (``os.rename``). Projects can never observe a partial
  SDK; a concurrent/cancelled install leaves previous SDKs usable.
* An existing verified-identical install is reused; a corrupt destination fails
  with an explicit repair operation rather than being silently overwritten.

Normal open/build/run never calls into install — resolution only reads already
published SDKs (P05/P09).
"""

from __future__ import annotations

import hashlib
import os
import shutil
import tarfile
import tempfile
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, Optional

from .errors import (
    ARCHIVE_INVALID,
    DIGEST_MISMATCH,
    INSTALL_CANCELLED,
    SDK_CORRUPT,
    SDK_NOT_FOUND,
    ToolingError,
)
from .identity import SdkIdentity, load_prefix_manifest

# Conservative archive bounds. These guard against zip/tar bombs and runaway
# installs; they are generous for a real SDK but finite.
MAX_COMPRESSED_BYTES = 2 * 1024 * 1024 * 1024  # 2 GiB on-disk archive
MAX_EXPANDED_BYTES = 8 * 1024 * 1024 * 1024  # 8 GiB expanded
MAX_ENTRY_COUNT = 500_000
MAX_SINGLE_ENTRY_BYTES = 4 * 1024 * 1024 * 1024

STAGING_SUFFIX = ".staging"
CORRUPT_MARKER = ".ludus-corrupt"


def default_store_root() -> Path:
    env = os.environ.get("LUDUS_SDK_STORE")
    if env:
        return Path(env)
    xdg = os.environ.get("XDG_DATA_HOME")
    base = Path(xdg) if xdg else Path.home() / ".local" / "share"
    return base / "ludus"


@dataclass
class InstalledSdk:
    identity: SdkIdentity
    prefix: Path
    digest: str


def sha256_file(path: Path, *, chunk: int = 1024 * 1024) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        while True:
            block = fh.read(chunk)
            if not block:
                break
            h.update(block)
    return h.hexdigest()


class CancelToken:
    """Cooperative cancellation. Install checks it at coarse boundaries."""

    def __init__(self) -> None:
        self._cancelled = False

    def cancel(self) -> None:
        self._cancelled = True

    @property
    def cancelled(self) -> bool:
        return self._cancelled

    def check(self) -> None:
        if self._cancelled:
            raise ToolingError(INSTALL_CANCELLED, "installation cancelled")


class SdkStore:
    def __init__(self, root: Optional[Path] = None) -> None:
        self.root = Path(root) if root is not None else default_store_root()
        self.sdk_root = self.root / "sdks"
        self.tmp_root = self.root / "tmp"  # writable downloads/staging, separate from immutable SDKs
        self.lock_root = self.root / "locks"

    # --- Layout -------------------------------------------------------------
    def _slug(self, identity: SdkIdentity, digest: str) -> str:
        # Address by variant + short digest. Immutable and content-addressed so
        # two identical installs collapse to one directory.
        return f"{identity.flavor}-{identity.sdk_variant}-{digest[:16]}".replace("/", "_")

    def prefix_for(self, identity: SdkIdentity, digest: str) -> Path:
        return self.sdk_root / self._slug(identity, digest)

    # --- Listing ------------------------------------------------------------
    def list_installed(self) -> list[InstalledSdk]:
        out: list[InstalledSdk] = []
        if not self.sdk_root.is_dir():
            return out
        for entry in sorted(self.sdk_root.iterdir()):
            if not entry.is_dir() or entry.name.endswith(STAGING_SUFFIX):
                continue
            if (entry / CORRUPT_MARKER).exists():
                continue
            try:
                identity = load_prefix_manifest(entry)
            except ToolingError:
                continue
            digest_path = entry / ".ludus-digest"
            digest = digest_path.read_text().strip() if digest_path.is_file() else ""
            out.append(InstalledSdk(identity=identity, prefix=entry, digest=digest))
        return out

    def find_by_digest(self, digest: str) -> Optional[InstalledSdk]:
        for sdk in self.list_installed():
            if sdk.digest == digest:
                return sdk
        return None

    # --- Cooperative lock ---------------------------------------------------
    @contextmanager
    def _install_lock(self, slug: str) -> Iterator[None]:
        import fcntl

        self.lock_root.mkdir(parents=True, exist_ok=True)
        lock_path = self.lock_root / f"{slug}.lock"
        fh = open(lock_path, "w")
        try:
            try:
                fcntl.flock(fh.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError as exc:
                raise ToolingError(
                    "Busy", f"another installer holds the lock for {slug}"
                ) from exc
            yield
        finally:
            try:
                fcntl.flock(fh.fileno(), fcntl.LOCK_UN)
            finally:
                fh.close()

    # --- Archive validation -------------------------------------------------
    def validate_archive(self, archive: Path) -> None:
        """Validate archive bounds/containment WITHOUT extracting (P05)."""
        if not archive.is_file():
            raise ToolingError(ARCHIVE_INVALID, f"archive not found: {archive}")
        compressed = archive.stat().st_size
        if compressed > MAX_COMPRESSED_BYTES:
            raise ToolingError(ARCHIVE_INVALID, "archive exceeds compressed-size bound")
        if not tarfile.is_tarfile(archive):
            raise ToolingError(ARCHIVE_INVALID, "archive is not a supported tar archive")
        total = 0
        count = 0
        with tarfile.open(archive, "r:*") as tf:
            for member in tf:
                count += 1
                if count > MAX_ENTRY_COUNT:
                    raise ToolingError(ARCHIVE_INVALID, "archive has too many entries")
                _check_member(member)
                if member.isreg():
                    if member.size > MAX_SINGLE_ENTRY_BYTES:
                        raise ToolingError(ARCHIVE_INVALID, "archive entry exceeds single-entry bound")
                    total += member.size
                    if total > MAX_EXPANDED_BYTES:
                        raise ToolingError(ARCHIVE_INVALID, "archive exceeds expanded-size bound")

    # --- Install ------------------------------------------------------------
    def install_archive(
        self,
        archive: Path,
        *,
        expected_digest: Optional[str] = None,
        cancel: Optional[CancelToken] = None,
        repair: bool = False,
    ) -> InstalledSdk:
        """Install a local SDK archive into the store atomically."""
        cancel = cancel or CancelToken()
        archive = Path(archive)
        self.validate_archive(archive)
        cancel.check()

        digest = sha256_file(archive)
        if expected_digest is not None and digest.lower() != expected_digest.lower():
            raise ToolingError(
                DIGEST_MISMATCH,
                f"archive digest {digest} does not match expected {expected_digest}",
            )
        cancel.check()

        # Reuse an already-published identical install.
        existing = self.find_by_digest(digest)
        if existing is not None and not repair:
            return existing

        self.sdk_root.mkdir(parents=True, exist_ok=True)
        self.tmp_root.mkdir(parents=True, exist_ok=True)

        # Peek identity by extracting only the manifest to a scratch area, so the
        # final directory name (which encodes identity) is known before publish.
        staging = Path(tempfile.mkdtemp(prefix="sdk-stage-", dir=str(self.tmp_root)))
        try:
            with tarfile.open(archive, "r:*") as tf:
                for member in tf:
                    _check_member(member)
                cancel.check()
                _safe_extractall(tf, staging, cancel)

            # Locate the prefix root inside the staging dir (archive may wrap a
            # single top-level directory).
            prefix_src = _locate_prefix(staging)
            identity = load_prefix_manifest(prefix_src)
            (prefix_src / ".ludus-digest").write_text(digest + "\n")

            dest = self.prefix_for(identity, digest)
            slug = dest.name
            with self._install_lock(slug):
                # Re-check after acquiring the lock: a competing installer may
                # have just published the identical SDK.
                if dest.exists():
                    if (dest / CORRUPT_MARKER).exists() or repair:
                        _remove_tree(dest)
                    else:
                        # Validate the published copy; reuse if intact.
                        try:
                            load_prefix_manifest(dest)
                            return InstalledSdk(identity=identity, prefix=dest, digest=digest)
                        except ToolingError:
                            (dest / CORRUPT_MARKER).write_text("corrupt\n")
                            raise ToolingError(
                                SDK_CORRUPT,
                                f"published SDK at {dest} is corrupt; rerun install --repair",
                            )
                cancel.check()
                # Atomic publish: rename the validated prefix into place.
                final_staging = dest.with_name(dest.name + STAGING_SUFFIX)
                if final_staging.exists():
                    _remove_tree(final_staging)
                shutil.move(str(prefix_src), str(final_staging))
                os.rename(final_staging, dest)
            return InstalledSdk(identity=identity, prefix=dest, digest=digest)
        except ToolingError:
            raise
        finally:
            _remove_tree(staging)

    def resolve_installed(self, digest: str) -> InstalledSdk:
        sdk = self.find_by_digest(digest)
        if sdk is None:
            raise ToolingError(
                SDK_NOT_FOUND,
                f"no installed SDK with digest {digest[:16]}…; run 'ludus sdk install'",
            )
        return sdk

    def remove(self, digest: str) -> bool:
        sdk = self.find_by_digest(digest)
        if sdk is None:
            return False
        _remove_tree(sdk.prefix)
        return True


# --- Archive member safety ----------------------------------------------------


def _check_member(member: tarfile.TarInfo) -> None:
    name = member.name
    if name in ("", ".") or name.startswith("/") or name.startswith("\\"):
        raise ToolingError(ARCHIVE_INVALID, f"archive entry has an absolute/empty name: {name!r}")
    # Reject traversal in any normalized segment.
    norm = os.path.normpath(name)
    if norm.startswith("..") or norm.startswith("/") or (".." in Path(norm).parts):
        raise ToolingError(ARCHIVE_INVALID, f"archive entry escapes the destination: {name!r}")
    if member.isdev() or member.ischr() or member.isblk() or member.isfifo():
        raise ToolingError(ARCHIVE_INVALID, f"unsupported archive entry type: {name!r}")
    if member.issym() or member.islnk():
        target = member.linkname
        # Links must resolve inside the archive root; reject absolute/escaping.
        if os.path.isabs(target):
            raise ToolingError(ARCHIVE_INVALID, f"archive link is absolute: {name!r} -> {target!r}")
        joined = os.path.normpath(os.path.join(os.path.dirname(name), target))
        if joined.startswith("..") or joined.startswith("/"):
            raise ToolingError(
                ARCHIVE_INVALID, f"archive link escapes the destination: {name!r} -> {target!r}"
            )


def _safe_extractall(tf: tarfile.TarFile, dest: Path, cancel: CancelToken) -> None:
    dest = dest.resolve()
    members = tf.getmembers()
    for member in members:
        cancel.check()
        _check_member(member)
        target = (dest / member.name).resolve()
        if not str(target).startswith(str(dest) + os.sep) and target != dest:
            raise ToolingError(ARCHIVE_INVALID, f"archive entry escapes destination: {member.name!r}")

    # Prefer the hardened tar data filter when available (Python 3.12+). On
    # 3.10/3.11 that keyword is absent, so extract member-by-member ourselves
    # with a guard that re-checks each materialized target does not escape via a
    # symlink/parent created earlier in the SAME extraction (static pre-checks
    # above cannot see runtime-materialized links). This keeps containment equal
    # across all supported Pythons rather than trusting unfiltered extractall.
    try:
        tf.extractall(dest, filter="data")  # type: ignore[call-arg]
        return
    except TypeError:
        pass

    for member in members:
        cancel.check()
        # Resolve the parent WITHOUT following into an escaping symlink: the
        # realpath of the destination of this member must stay within dest.
        member_path = (dest / member.name)
        parent = member_path.parent
        real_parent = os.path.realpath(parent)
        if real_parent != str(dest) and not real_parent.startswith(str(dest) + os.sep):
            raise ToolingError(
                ARCHIVE_INVALID,
                f"archive entry's parent escapes the destination at extraction time: {member.name!r}",
            )
        tf.extract(member, dest)


def _locate_prefix(staging: Path) -> Path:
    """Find the install prefix inside a staging dir.

    An SDK archive may wrap its prefix in a single top-level directory. The
    prefix is the directory that contains ``share/Ludus/LudusSdkManifest.json``.
    """
    from .identity import MANIFEST_RELPATH

    if (staging / MANIFEST_RELPATH).is_file():
        return staging
    entries = [p for p in staging.iterdir() if p.is_dir()]
    if len(entries) == 1 and (entries[0] / MANIFEST_RELPATH).is_file():
        return entries[0]
    raise ToolingError(ARCHIVE_INVALID, "archive does not contain an SDK manifest at a known location")


def _remove_tree(path: Path) -> None:
    try:
        if path.is_dir():
            shutil.rmtree(path, ignore_errors=True)
        elif path.exists():
            path.unlink()
    except OSError:
        pass
