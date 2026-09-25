#!/usr/bin/env python3
"""Enforce the Ludus foundational-header include boundary.

A narrow, hard gate (deliberately small so it does not become the noisy
include-cleaner ADR 0005 rejected). It scans header text and fails only on
unambiguous violations of the layering rules in
docs/architecture/foundational-headers.md (Section 9, Section 13) and ADR 0007:

  1. The foundational headers (Band 0 + Band 1: core.h, config.h, compiler.h,
     types.h) MUST NOT include a heavy STL header, any container/string header
     (Ludus or std), or anything above the leaf FoundationBase module. They are
     the universally-available layer and must stay effectively free to parse.

  2. Any PUBLIC header (under modules/**/include or tools/**/include) MUST NOT
     include a heavy STL header (ADR 0004) and MUST NOT include a private
     implementation header (a path containing 'internal/' or 'src/').

Exit code 0 = clean, 1 = violations found (with file:line and the rule).

Usage: check_foundational_includes.py [repo_root]
"""

import re
import sys
from pathlib import Path

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')

# Heavy STL headers banned from ALL public headers (ADR 0004 / AGENTS.md).
HEAVY_STL = {
    "format", "chrono", "filesystem", "regex", "iostream", "sstream",
    "fstream", "locale", "random", "ranges",
}

# The foundational header set: the universally-available layer. It must pull
# only trivially-cheap std headers and never a container/string/heavy facility.
FOUNDATION_HEADERS = {
    "core.h", "config.h", "compiler.h", "types.h",
}

# std headers that are forbidden specifically inside the foundational set,
# on top of HEAVY_STL: strings and containers must never be implicit.
FOUNDATION_EXTRA_FORBIDDEN_STL = {
    "string", "string_view", "vector", "array", "map", "unordered_map",
    "set", "unordered_set", "deque", "list", "memory", "span",
}


def public_headers(root: Path):
    seen = set()
    for pattern in ("modules/*/*/include", "tools/*/include"):
        for include_dir in root.glob(pattern):
            for suffix in ("*.h", "*.hpp"):
                for header in include_dir.rglob(suffix):
                    if "internal" in header.parts or header in seen:
                        continue
                    seen.add(header)
                    yield header


def includes(header: Path):
    for lineno, line in enumerate(header.read_text(encoding="utf-8").splitlines(), start=1):
        match = INCLUDE_RE.match(line)
        if match:
            yield lineno, match.group(1)


def main() -> int:
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    violations: list[str] = []

    for header in public_headers(root):
        rel = header.relative_to(root)
        is_foundation = header.name in FOUNDATION_HEADERS and "foundation/base" in header.as_posix()

        for lineno, target in includes(header):
            base = target.rsplit("/", 1)[-1]
            is_std = "/" not in target and "." not in target  # <format>, <vector>, ...
            std_name = target if is_std else None

            # Rule 2a: no heavy STL in any public header.
            if std_name in HEAVY_STL:
                violations.append(
                    f"{rel}:{lineno}: heavy STL <{target}> in a public header "
                    f"(type-erase behind a .cpp; ADR 0004)"
                )
            # Rule 2b: no private implementation header from a public header.
            if target.startswith("ludus/") and ("/internal/" in target or "/src/" in target):
                violations.append(
                    f"{rel}:{lineno}: public header includes private header <{target}> "
                    f"(Section 13)"
                )

            if is_foundation:
                # Rule 1: the foundational set forbids strings/containers/memory
                # and any ludus header outside foundation/base.
                if std_name in FOUNDATION_EXTRA_FORBIDDEN_STL:
                    violations.append(
                        f"{rel}:{lineno}: foundational header pulls <{target}> "
                        f"(strings/containers must not be implicit; Section 9)"
                    )
                if target.startswith("ludus/") and "foundation/base/" not in target:
                    violations.append(
                        f"{rel}:{lineno}: foundational header depends on non-Base "
                        f"header <{target}> (Band 0/1 is the leaf; Section 9)"
                    )

    if violations:
        print("Foundational include-boundary violations:\n")
        for violation in sorted(violations):
            print(f"  {violation}")
        print(f"\n{len(violations)} violation(s). See docs/architecture/foundational-headers.md.")
        return 1
    print("Foundational include boundary OK.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
