"""Version-1 reproducible byte packs; format contract: docs/architecture/filesystem.md.

Thanks to Yann Collet, LZ4 Block Format Description (2022-07-31), v1.10.0:
https://github.com/lz4/lz4/blob/v1.10.0/doc/lz4_Block_format.md . This original
simple greedy encoder emits independent bounded blocks with final literals;
no upstream implementation copied. Thanks to L. Peter Deutsch, RFC 1952,
sec. 2.3.1, for CRC32 semantics (not GZIP framing). Thanks to David L. Koenig,
Game Programming Gems 6, sec. 1.9, pp. 103-108, for separating trace-derived
payload placement from sorted lookup. This builder claims no loading speedup.
Thanks to Microsoft, Naming Files, Paths, and Namespaces (Win32), for reserved
names/component restrictions; and the Python Software Foundation, unicodedata,
for normalization collision checking. https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
https://docs.python.org/3/library/unicodedata.html . Names are checked, not rewritten.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import io
import json
import os
from pathlib import Path
import stat
import struct
import tempfile
from typing import BinaryIO, Callable
import unicodedata

MAGIC = b"LUDPACK\0"
HEADER = struct.Struct("<8sIIIIIIQQQQIIQ")
ENTRY = struct.Struct("<IIIIQQ")
BLOCK = struct.Struct("<QIIII")
MAX_PATH = 1024
MAX_BLOCK = 65536
MAX_FILES = 65536
MAX_BLOCKS = 262144
MAX_PACK = 8 * 1024**3
MAX_INDEX = 32 * 1024**2
MAX_FILE = 1024**3
MAX_DECODED = 16 * 1024**3
BUILDER_VERSION = 1


class PackError(ValueError):
    """Invalid pack input or a configured resource budget violation."""


def portable_path(path: str) -> bytes:
    if not isinstance(path, str):
        raise PackError("paths must be strings")
    try:
        encoded = path.encode("utf-8", errors="strict")
    except UnicodeError as error:
        raise PackError("path must be valid UTF-8") from error
    if not encoded or len(encoded) > MAX_PATH or any(byte < 32 or byte == 127 for byte in encoded):
        raise PackError(f"invalid bounded path: {path!r}")
    if any(char in path for char in '\\:<>"|?*'):
        raise PackError(f"nonportable path: {path!r}")
    reserved = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"}
    reserved.update(prefix + digit for prefix in ("COM", "LPT") for digit in "123456789¹²³")
    for component in path.split("/"):
        if component in ("", ".", "..") or component.endswith((".", " ")):
            raise PackError(f"invalid path component: {path!r}")
        if len(component.encode("utf-16-le")) // 2 > 255:
            raise PackError(f"component exceeds portable UTF-16 length: {path!r}")
        if component.split(".")[0].rstrip(" .").upper() in reserved:
            raise PackError(f"reserved device name: {path!r}")
    return encoded


def canonical_paths(paths: list[str]) -> list[str]:
    if len(paths) > MAX_FILES or len(paths) != len(set(paths)):
        raise PackError("too many files or duplicate logical paths")
    keys = {path: portable_path(path) for path in paths}
    # Check every directory prefix too: A/x and a/y alias directories on some
    # hosts even when their full logical file keys do not collide.
    equivalence: dict[str, tuple[str, bool]] = {}
    for path in paths:
        components = path.split("/")
        for end in range(1, len(components) + 1):
            original = "/".join(components[:end])
            normalized = unicodedata.normalize("NFD", unicodedata.normalize("NFD", original).casefold())
            is_file = end == len(components)
            previous = equivalence.get(normalized)
            if previous is not None and previous != (original, is_file):
                raise PackError(f"host case/normalization or file/directory collision: {previous[0]!r}, {original!r}")
            equivalence[normalized] = (original, is_file)
    return sorted(paths, key=keys.__getitem__)


def layout_paths(canonical: list[str], layout: dict | None) -> list[str]:
    if layout is None:
        return canonical.copy()
    if not isinstance(layout, dict) or layout.get("version") != 1 or set(layout) != {"version", "paths"}:
        raise PackError("layout must contain version=1 and paths only")
    selected = layout["paths"]
    if not isinstance(selected, list) or any(not isinstance(path, str) for path in selected):
        raise PackError("layout paths must be a list of strings")
    if len(selected) != len(set(selected)) or not set(selected).issubset(canonical):
        raise PackError("layout has duplicate or unknown paths")
    used = set(selected)
    return selected + [path for path in canonical if path not in used]


def _extension(output: bytearray, length: int) -> None:
    while length >= 255:
        output.append(255)
        length -= 255
    output.append(length)


def encode_lz4(data: bytes) -> bytes:
    """Deterministic greedy matches; blocks stay within 64 KiB and use no dictionary."""
    output = bytearray()
    previous: dict[bytes, int] = {}
    anchor = cursor = 0
    while cursor <= len(data) - 12:
        key = data[cursor:cursor + 4]
        match = previous.get(key)
        previous[key] = cursor
        if match is None or cursor - match > 65535:
            cursor += 1
            continue
        length = 4
        while cursor + length < len(data) - 5 and data[match + length] == data[cursor + length]:
            length += 1
        literals = cursor - anchor
        encoded_match = length - 4
        output.append((min(literals, 15) << 4) | min(encoded_match, 15))
        if literals >= 15:
            _extension(output, literals - 15)
        output.extend(data[anchor:cursor])
        output.extend(struct.pack("<H", cursor - match))
        if encoded_match >= 15:
            _extension(output, encoded_match - 15)
        cursor += length
        anchor = cursor
    literals = len(data) - anchor
    output.append(min(literals, 15) << 4)
    if literals >= 15:
        _extension(output, literals - 15)
    output.extend(data[anchor:])
    return bytes(output)


def build_stream(paths: list[str], open_source: Callable[[str], BinaryIO], destination: BinaryIO,
                 *, block_size: int = MAX_BLOCK, compress: bool = True,
                 layout: dict | None = None) -> dict:
    """Stream sources into a bounded spool, then emit header, sorted index and payload.

    The caller owns destination publication. No timestamp, machine path or random
    value enters the pack bytes. Sources must remain stable for the duration.
    """
    if block_size < 256 or block_size > MAX_BLOCK or block_size & (block_size - 1):
        raise PackError("block size must be a power of two from 256 through 65536")
    canonical = canonical_paths(paths)
    physical = layout_paths(canonical, layout)
    records: dict[str, tuple[int, list[tuple[int, int, int, int, int]]]] = {}
    total = blocks = 0
    with tempfile.TemporaryFile() as payload:
        for path in physical:
            decoded = 0
            chunks: list[tuple[int, int, int, int, int]] = []
            with open_source(path) as source:
                while True:
                    data = source.read(block_size)
                    if not data:
                        break
                    # Binary regular-file inputs and BytesIO fill reads. Do not
                    # admit arbitrary short-read streams with invalid interior blocks.
                    if len(data) > block_size:
                        raise PackError("source violated bounded read contract")
                    if len(data) < block_size and source.read(1):
                        raise PackError("source must fill reads up to EOF")
                    decoded += len(data)
                    total += len(data)
                    blocks += 1
                    if decoded > MAX_FILE or total > MAX_DECODED or blocks > MAX_BLOCKS:
                        raise PackError("decoded file/total/block budget exceeded")
                    packed = encode_lz4(data) if compress else data
                    codec = 1 if compress and len(packed) < len(data) else 0
                    if codec == 0:
                        packed = data
                    chunks.append((payload.tell(), len(packed), len(data), codec, binascii.crc32(data)))
                    payload.write(packed)
                    if payload.tell() > MAX_PACK:
                        raise PackError("pack storage budget exceeded")
            records[path] = (decoded, chunks)
        index = bytearray()
        first = 0
        for path in canonical:
            decoded, chunks = records[path]
            key = path.encode("utf-8")
            index.extend(ENTRY.pack(len(key), first, len(chunks), 0, decoded, sum(chunk[1] for chunk in chunks)))
            index.extend(key)
            first += len(chunks)
        payload_start = HEADER.size + len(index) + blocks * BLOCK.size
        for path in canonical:
            for offset, stored, decoded, codec, checksum in records[path][1]:
                index.extend(BLOCK.pack(payload_start + offset, stored, decoded, codec, checksum))
        if len(index) > MAX_INDEX:
            raise PackError("serialized index budget exceeded")
        size = payload_start + payload.tell()
        if size > MAX_PACK:
            raise PackError("pack storage budget exceeded")
        fields = (MAGIC, 1, HEADER.size, 0, block_size, len(canonical), blocks,
                  HEADER.size, len(index), payload_start, size, binascii.crc32(index), 0, 0)
        header = bytearray(HEADER.pack(*fields))
        struct.pack_into("<I", header, 68, binascii.crc32(header))
        destination.write(header)
        destination.write(index)
        payload.seek(0)
        while chunk := payload.read(65536):
            destination.write(chunk)
    return {"format_version": 1, "builder_version": BUILDER_VERSION,
            "unicode_version": unicodedata.unidata_version, "files": len(canonical), "blocks": blocks,
            "decoded_bytes": total, "pack_bytes": size, "block_bytes": block_size,
            "codec_policy": "lz4-if-smaller" if compress else "raw",
            "layout_sha256": hashlib.sha256(json.dumps(layout, sort_keys=True, separators=(",", ":"),
                                                      ensure_ascii=False).encode()).hexdigest() if layout else None}


def build_bytes(entries: list[tuple[str, bytes]], **options) -> bytes:
    """Small tooling/test helper. The streaming CLI does not buffer whole source files."""
    paths = [path for path, _ in entries]
    sources = dict(entries)
    output = io.BytesIO()
    build_stream(paths, lambda path: io.BytesIO(sources[path]), output, **options)
    return output.getvalue()


def source_paths(root: Path) -> list[str]:
    found: list[str] = []
    for parent, directories, files in os.walk(root, followlinks=False):
        for name in [*directories, *files]:
            path = Path(parent) / name
            if path.is_symlink():
                raise PackError(f"child symlink refused: {path}")
        for name in files:
            path = Path(parent) / name
            if not stat.S_ISREG(path.stat().st_mode):
                raise PackError(f"nonregular source refused: {path}")
            found.append(path.relative_to(root).as_posix())
            if len(found) > MAX_FILES:
                raise PackError("file count budget exceeded")
    return found


class StableSource:
    """Check ordinary source mutation; trusted input roots are not a hostile-process sandbox."""
    def __init__(self, path: Path):
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
        descriptor = os.open(path, flags)
        self.file = os.fdopen(descriptor, "rb")
        self.before = os.fstat(descriptor)
        if not stat.S_ISREG(self.before.st_mode) or self.before.st_size > MAX_FILE:
            self.file.close()
            raise PackError(f"nonregular or oversized source: {path}")

    def __enter__(self):
        return self.file

    def __exit__(self, kind, value, traceback):
        try:
            after = os.fstat(self.file.fileno())
            if kind is None and (after.st_size, after.st_mtime_ns) != (self.before.st_size, self.before.st_mtime_ns):
                raise PackError("source changed while building")
        finally:
            self.file.close()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build a deterministic v1 Ludus pack from a trusted directory")
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--layout", type=Path, help='JSON {"version":1,"paths":[...]}: listed files first, others sorted')
    parser.add_argument("--block-bytes", type=int, default=MAX_BLOCK)
    parser.add_argument("--raw", action="store_true", help="Disable independent LZ4 compression")
    args = parser.parse_args(argv)
    temporary: Path | None = None
    try:
        root = args.source.resolve(strict=True)
        if not root.is_dir():
            raise PackError("source must be a directory")
        output = args.output.resolve()
        if output == root or root in output.parents:
            raise PackError("output must be outside the source tree")
        layout = None
        if args.layout:
            if args.layout.stat().st_size > 8 * 1024**2:
                raise PackError("layout manifest budget exceeded")
            layout = json.loads(args.layout.read_text(encoding="utf-8"))
        paths = source_paths(root)
        descriptor, name = tempfile.mkstemp(prefix=output.name + ".", suffix=".tmp", dir=output.parent)
        temporary = Path(name)
        with os.fdopen(descriptor, "wb") as destination:
            report = build_stream(paths, lambda path: StableSource(root / path), destination,
                                  block_size=args.block_bytes, compress=not args.raw, layout=layout)
        # Offline build publication only; this is not the engine's F5 durable-save API.
        os.replace(temporary, output)
        temporary = None
        print(json.dumps(report, sort_keys=True))
        return 0
    except (OSError, ValueError, UnicodeError) as error:
        print(f"pack: {error}", file=__import__("sys").stderr)
        return 1
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
