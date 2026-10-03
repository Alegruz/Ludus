"""One real host-owned configuration asset, using the public RHI clear path.

Sources are project-relative JSON; cooked payloads are immutable and bounded.
The host owns a value copy, so no GPU submission references the cache file.
"""
from __future__ import annotations
import hashlib
import math
import os
from pathlib import Path
import struct
import tempfile

from play_documents import DocumentError, contained_path, decode, read_bounded
from ludus_tools.buildlock import BuildTreeLock

FRAME_CLEAR_ID = "1000000000000001"


def cook_frame_clear(data: bytes) -> bytes:
    obj = decode(data)
    if set(obj) != {"version", "kind", "rgba"} or type(obj["version"]) is not int or obj["version"] != 1 or obj["kind"] != "frame_clear":
        raise DocumentError("only version 1 frame_clear configuration assets are supported")
    channels = obj["rgba"]
    if not isinstance(channels, list) or len(channels) != 4 or any(
            type(value) not in (int, float) or not math.isfinite(value) or not 0 <= value <= 1 for value in channels):
        raise DocumentError("rgba must contain four finite values in [0,1]")
    return b"LCLR" + struct.pack("<I4f", 1, *channels)


def artifact_digest(payload: bytes) -> str:
    value = 0xCBF29CE484222325
    for byte in payload:
        value = ((value ^ byte) * 0x100000001B3) & (2**64-1)
    return f"{value:016x}"


def publish_frame_clear(project: Path, source: str) -> dict:
    path = contained_path(project, source)
    if path.is_symlink() or not path.is_file():
        raise DocumentError("configuration source must be a regular project file")
    data = read_bounded(path)
    cooked = cook_frame_clear(data)
    source_hash = hashlib.sha256(data).hexdigest()
    cooked_hash = hashlib.sha256(cooked).hexdigest()
    root = contained_path(project, ".ludus/assets/frame-clear")
    root.mkdir(parents=True, exist_ok=True)
    artifact = root / (cooked_hash + ".bin")
    with BuildTreeLock(root):
        if artifact.exists():
            if artifact.is_symlink() or read_bounded(artifact) != cooked:
                raise DocumentError("immutable configuration cache disagrees with its content hash")
        else:
            fd, temporary = tempfile.mkstemp(prefix=".staging-", dir=root)
            try:
                with os.fdopen(fd, "wb") as stream:
                    stream.write(cooked)
                    stream.flush()
                    os.fsync(stream.fileno())
                os.chmod(temporary, 0o444)
                os.replace(temporary, artifact)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
        # The host receives a 24-byte copy, never a file reference. Retain two
        # cache artifacts for diagnosis; no in-flight user leases are needed.
        owned = sorted((item for item in root.glob("*.bin") if len(item.stem) == 64 and
                        all(c in "0123456789abcdef" for c in item.stem) and not item.is_symlink() and item.is_file()),
                       key=lambda item: item.stat().st_mtime_ns, reverse=True)
        others = [item for item in owned if item != artifact]
        keep = {artifact, *others[:1]}
        for item in owned:
            if item not in keep:
                item.unlink()
    if hashlib.sha256(read_bounded(path)).hexdigest() != source_hash:
        raise DocumentError("configuration source changed during import; retry the latest revision")
    return dict(asset_id=FRAME_CLEAR_ID, artifact=cooked.hex(), digest=artifact_digest(cooked),
                source_sha256=source_hash, cooked_sha256=cooked_hash)
