"""Generate small compressed byte fixtures through the shipping builder, plus fuzz seeds."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/python"))
from pack_builder import build_bytes


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("header", type=Path)
    parser.add_argument("--corpus", type=Path)
    args = parser.parse_args()
    entries = [("raw", bytes(range(251))), ("empty", b""), ("compressed", b"A" * 70000 + b"tail-sentinel")]
    fixtures = {"kPack": build_bytes(entries), "kReorderedPack": build_bytes(entries, layout={"version": 1, "paths": ["raw"]}),
                "kEmptyPack": build_bytes([]),
                "kSmallBlockPack": build_bytes([("pattern", b"abcabcde" * 200)], block_size=256)}
    args.header.parent.mkdir(parents=True, exist_ok=True)
    lines = ["#pragma once", "#include <ludus/foundation/base/types.h>", "namespace pack_fixture {",
             "using ludus::foundation::uint8;"]
    for name, data in fixtures.items():
        lines.append(f"inline constexpr uint8 {name}[] = {{")
        lines.extend("    " + ", ".join(str(byte) for byte in data[start:start + 24]) + "," for start in range(0, len(data), 24))
        lines.append("};")
        if args.corpus:
            args.corpus.mkdir(parents=True, exist_ok=True)
            (args.corpus / name).write_bytes(data)
    lines.append("}")
    args.header.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
