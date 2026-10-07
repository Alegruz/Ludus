"""Deterministic mixed assets for the opt-in F4 benchmark; outputs remain in out/."""
import argparse
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/python"))
from pack_builder import build_stream, source_paths, StableSource


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    corpus = args.directory / "loose"
    for kind, count, size in [("small", 64, 4096), ("large", 8, 2 * 1024 * 1024)]:
        (corpus / kind).mkdir(parents=True, exist_ok=True)
        for index in range(count):
            # Repeatable compressible blocks, distinct files, stable portable keys.
            data = bytes((byte + index) % 256 for byte in range(256)) * (size // 256)
            (corpus / kind / f"{index:02d}").write_bytes(data)
    for name, compress in [("raw.pack", False), ("lz4.pack", True)]:
        with (args.directory / name).open("wb") as destination:
            build_stream(source_paths(corpus), lambda path: StableSource(corpus / path), destination, compress=compress)


if __name__ == "__main__":
    main()
