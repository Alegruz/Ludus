#!/usr/bin/env python3
"""Make an explicitly tone-mapped PNG preview of a complete LUHDR01 capture."""

import argparse
import importlib.machinery
import importlib.util
import math
from pathlib import Path
import struct
import zlib


def preview(capture, output, exposure=1.0):
    if not math.isfinite(exposure) or exposure <= 0:
        raise ValueError("exposure must be finite and positive")
    loader = importlib.machinery.SourceFileLoader(
        "reference_hdr",
        str(Path(__file__).resolve().parents[2] / "scripts/reference-hdr"),
    )
    spec = importlib.util.spec_from_loader(loader.name, loader)
    decoder = importlib.util.module_from_spec(spec)
    loader.exec_module(decoder)
    metadata, pixels = decoder.decode(capture.read_bytes())
    if metadata["partial"]:
        raise ValueError("preview requires a complete capture")

    def channel(value):
        value *= exposure
        # Simple documented photographic compression, then IEC sRGB encoding.
        linear = value / (1 + value)
        encoded = (
            12.92 * linear
            if linear <= 0.0031308
            else 1.055 * linear ** (1 / 2.4) - 0.055
        )
        return round(255 * encoded)

    width, height = metadata["width"], metadata["height"]
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for pixel in pixels[y * width : (y + 1) * width]:
            rows.extend(channel(c) for c in pixel[:3])

    def chunk(name, data):
        return (
            struct.pack(">I", len(data))
            + name
            + data
            + struct.pack(">I", zlib.crc32(name + data))
        )

    output.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">2I5B", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows))
        + chunk(b"IEND", b"")
    )
    return metadata


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--exposure", type=float, default=1.0)
    args = parser.parse_args()
    preview(args.capture, args.output, args.exposure)
