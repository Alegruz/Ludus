#!/usr/bin/env python3
"""Checkout entry point for the installed, shared scene converter."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/python"))
from ludus_tools.scene_cook import *  # noqa: F403 - compatibility with converter tests

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(cook(args.source, args.output), indent=2))
    except (ValueError, OSError, KeyError, IndexError, struct.error) as error:
        parser.exit(1, f"conversion failed: {error}\n")
