"""Exercise the installed-SDK sample with independently encoded valid/bad bundles."""

import importlib.machinery
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def fixture():
    data = bytearray(b"LUSCN01\0")
    data += struct.pack("<5I10f", 1, 6, 8, 3, 1, 0, 0, 2, 0, 0, 0, 0, 1, 0, 1)
    for _ in range(3):
        data += struct.pack("<3I4B", 1, 1, 4, 255, 255, 255, 255)
    for index in range(8):
        data += struct.pack(
            "<I5f",
            0xFFFFFFFF if index >= 4 else 0,
            0,
            1,
            *((0.5, 0.5, 0.5) if index >= 4 else (0, 0, 0)),
        )
    for _ in range(6):
        data += struct.pack("<2I", 4, 6)
        for x, y in ((-2, -2), (2, -2), (2, 2), (-2, 2)):
            data += struct.pack("<8f", x, y, 0, 0, 0, 1, 0, 0)
        data += struct.pack("<6I", 0, 1, 2, 0, 2, 3)
    data += struct.pack("<2I12f", 0, 4, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)
    return data


def main():
    binary = str(Path(sys.argv[1]).resolve())
    loader = importlib.machinery.SourceFileLoader(
        "reference_hdr",
        str(Path(__file__).resolve().parents[3] / "scripts/reference-hdr"),
    )
    decoder = importlib.util.module_from_spec(
        importlib.util.spec_from_loader(loader.name, loader)
    )
    loader.exec_module(decoder)
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        bundle = root / "scene.luscn"
        data = fixture()
        bundle.write_bytes(data)
        subprocess.run([binary, str(root), str(root), "8", "2"], check=True, timeout=30)
        metadata, pixels = decoder.decode((root / "capture.luhdr").read_bytes())
        assert (
            metadata["traversal"] == "cpu_oracle" and metadata["minimum_samples"] == 2
        )
        assert all(pixel[:3] == (0.5, 0.5, 0.5) for pixel in pixels), pixels
        good = (root / "capture.luhdr").read_bytes()
        for invalid in (
            "",
            "0",
            "1025",
            "-1",
            "99999999999999999999999999",
            "1" * 1025,
        ):
            result = subprocess.run(
                [binary, str(root), str(root), invalid, "2"], timeout=30
            )
            assert result.returncode == 2, result.returncode
            assert (root / "capture.luhdr").read_bytes() == good

        cases = [data[:7], data[:-1], data + b"x"]
        too_many = data[:]
        struct.pack_into("<I", too_many, 12, 1000000)
        cases.append(too_many)
        bad_float = data[:]
        struct.pack_into("<I", bad_float, 28, 0x7FC00000)
        cases.append(bad_float)
        bad_index = data[:]
        struct.pack_into("<I", bad_index, len(data) - 56, 6)
        cases.append(bad_index)
        for malformed in cases:
            bundle.write_bytes(malformed)
            result = subprocess.run(
                [binary, str(root), str(root), "8", "2"], timeout=30
            )
            assert result.returncode == 8, result.returncode
            assert (root / "capture.luhdr").read_bytes() == good
    print("Complete capture and six malformed-bundle rejection cases passed.")


if __name__ == "__main__":
    main()
