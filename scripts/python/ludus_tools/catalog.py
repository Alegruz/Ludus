"""Release catalog model and bounded SDK download (P05).

A publisher-controlled catalog maps an engine release + target + flavor to a
package URL, size and SHA256. The catalog and downloads use HTTPS. A project
descriptor can never supply an install URL or script — only a publisher catalog
can (design "SDK store and installation"). Checksums detect corruption; they are
not authenticity when the catalog itself is untrusted.

Remote metadata is fetched ONLY during explicit install/select operations.
Downloads are bounded (declared size + a hard cap) and streamed to the store's
writable temp area, kept separate from immutable final SDKs. The downloaded
archive then goes through the same strict `SdkStore.install_archive` validation
(bounds/containment/digest/manifest/atomic publish).
"""

from __future__ import annotations

import json
import os
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from .errors import ARCHIVE_INVALID, DIGEST_MISMATCH, MANIFEST_INVALID, SDK_NOT_FOUND, ToolingError

CATALOG_SCHEMA_VERSION = 1
MAX_CATALOG_BYTES = 4 * 1024 * 1024
# Absolute hard cap on a downloaded archive regardless of declared size.
MAX_DOWNLOAD_BYTES = 2 * 1024 * 1024 * 1024


@dataclass
class CatalogEntry:
    release: str
    revision: str
    target: str
    flavor: str
    url: str
    size: int
    sha256: str

    @staticmethod
    def from_json(obj: dict) -> "CatalogEntry":
        return CatalogEntry(
            release=str(obj["release"]),
            revision=str(obj.get("revision", "")),
            target=str(obj["target"]),
            flavor=str(obj["flavor"]),
            url=str(obj["url"]),
            size=int(obj["size"]),
            sha256=str(obj["sha256"]),
        )


@dataclass
class Catalog:
    schema_version: int
    entries: list[CatalogEntry]

    def find(self, release: str, target: str, flavor: str) -> Optional[CatalogEntry]:
        for e in self.entries:
            if e.release == release and e.target == target and e.flavor == flavor:
                return e
        return None


def parse_catalog_bytes(data: bytes) -> Catalog:
    if len(data) > MAX_CATALOG_BYTES:
        raise ToolingError(MANIFEST_INVALID, "catalog exceeds size bound")
    try:
        obj = json.loads(data.decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as exc:
        raise ToolingError(MANIFEST_INVALID, f"malformed catalog: {exc}") from exc
    if not isinstance(obj, dict) or obj.get("schema_version") != CATALOG_SCHEMA_VERSION:
        raise ToolingError(MANIFEST_INVALID, "unsupported catalog schema")
    entries = [CatalogEntry.from_json(e) for e in obj.get("sdks", [])]
    for e in entries:
        if not e.url.startswith("https://"):
            raise ToolingError(MANIFEST_INVALID, f"catalog URL must be HTTPS: {e.url!r}")
        if len(e.sha256) != 64:
            raise ToolingError(MANIFEST_INVALID, f"catalog entry has an invalid sha256 for {e.release}/{e.flavor}")
    return Catalog(schema_version=CATALOG_SCHEMA_VERSION, entries=entries)


def parse_catalog_file(path: Path) -> Catalog:
    return parse_catalog_bytes(Path(path).read_bytes())


def download_entry(entry: CatalogEntry, dest_dir: Path, *, opener=None) -> Path:
    """Download a catalog entry's archive into ``dest_dir`` with bounds + digest.

    ``opener`` is an injectable callable(url) -> binary file-like, defaulting to
    urllib over HTTPS, so tests can supply a local source without network I/O.
    The declared ``size`` and a hard cap bound the download; the SHA256 is
    verified before the archive is handed to the store for installation.
    """
    import hashlib

    if not entry.url.startswith("https://") and opener is None:
        raise ToolingError(MANIFEST_INVALID, "refusing to download a non-HTTPS URL")
    if entry.size <= 0 or entry.size > MAX_DOWNLOAD_BYTES:
        raise ToolingError(ARCHIVE_INVALID, "catalog entry size is out of bounds")

    dest_dir = Path(dest_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix="dl-", suffix=".tar.gz", dir=str(dest_dir))
    tmp = Path(tmp_name)
    h = hashlib.sha256()
    written = 0
    try:
        src = opener(entry.url) if opener is not None else _https_open(entry.url)
        try:
            with os.fdopen(fd, "wb") as out:
                while True:
                    chunk = src.read(1024 * 1024)
                    if not chunk:
                        break
                    written += len(chunk)
                    if written > entry.size or written > MAX_DOWNLOAD_BYTES:
                        raise ToolingError(ARCHIVE_INVALID, "download exceeded the declared/absolute size bound")
                    h.update(chunk)
                    out.write(chunk)
        finally:
            close = getattr(src, "close", None)
            if close is not None:
                close()
        if written != entry.size:
            raise ToolingError(ARCHIVE_INVALID, f"truncated download: {written} of {entry.size} bytes")
        if h.hexdigest().lower() != entry.sha256.lower():
            raise ToolingError(DIGEST_MISMATCH, "downloaded archive digest does not match the catalog")
        return tmp
    except ToolingError:
        tmp.unlink(missing_ok=True)
        raise
    except OSError as exc:
        tmp.unlink(missing_ok=True)
        raise ToolingError(ARCHIVE_INVALID, f"download failed: {exc}") from exc


def _https_open(url: str):
    import urllib.request

    if not url.startswith("https://"):
        raise ToolingError(MANIFEST_INVALID, "refusing to open a non-HTTPS URL")
    return urllib.request.urlopen(url, timeout=60)  # noqa: S310 - HTTPS enforced above
