"""Verify every installed/public engine header compiles standalone.

Header self-sufficiency contract (docs/architecture/foundational-headers.md
Section 14, ADR 0007): a public header must compile on its own, i.e. it must
include everything it uses and MUST NOT rely on the consumer having included
something else first. The one sanctioned universally-available header is
<ludus/foundation/base/core.h>; a public header that uses the foundational
vocabulary includes core.h (or the specific Band 0 header) itself rather than
assuming it arrived transitively.

This is a mechanical gate, not prose: for each public header under
modules/**/include/ludus/** it compiles a tiny translation unit that includes
ONLY that header (with -fsyntax-only), reusing the real project compile flags
and include paths from compile_commands.json so the check sees exactly what the
build sees (generated headers, -std, warnings-as-errors, etc.).

Usage: verify_header_self_sufficiency.py <build_dir>
"""

import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_compile_policy import compiler_options


# Public headers that are intentionally NOT standalone-compilable and are
# excluded with a documented reason. Keep this list empty unless a header has a
# genuine, explained reason (e.g. a fragment meant to be textually included).
KNOWN_NON_STANDALONE: dict[str, str] = {
    # wayland/window.h #errors out unless LUDUS_PLATFORM_WAYLAND is defined by
    # the build; it is only ever compiled in the Wayland backend TU. The build
    # graph covers it; a bare-include probe cannot define the backend macro.
    "ludus/platform/wayland/window.h": "requires LUDUS_PLATFORM_WAYLAND from the build system",
    "ludus/platform/headless/window.h": "requires LUDUS_PLATFORM_HEADLESS from the build system",
}


def public_headers(root: Path) -> list[Path]:
    headers: list[Path] = []
    for module_include in root.glob("modules/*/*/include"):
        headers.extend(module_include.rglob("*.h"))
        headers.extend(module_include.rglob("*.hpp"))
    for tool_include in root.glob("tools/*/include"):
        headers.extend(tool_include.rglob("*.h"))
        headers.extend(tool_include.rglob("*.hpp"))
    # detail/ headers ship with the SDK and are instantiation points, so they
    # must also be self-sufficient; internal/ headers are private and excluded.
    return sorted(h for h in headers if "internal" not in h.parts)


def logical_include(header: Path, root: Path) -> str:
    include_root = header
    for parent in header.parents:
        if parent.name == "include":
            include_root = parent
            break
    return str(header.relative_to(include_root))


def representative_entry(entries):
    # Any engine TU's flags carry the full project include graph and standard.
    for entry in entries:
        if entry["file"].endswith("base/src/version.cpp"):
            return entry
    return entries[0]


def main() -> int:
    build = Path(sys.argv[1]).resolve()
    root = build.parents[2] if (build.parents[2] / "modules").is_dir() else Path(__file__).resolve().parents[2]
    entries = json.loads((build / "compile_commands.json").read_text())
    options = compiler_options(representative_entry(entries))
    directory = representative_entry(entries)["directory"]

    failures: list[str] = []
    checked = 0
    for header in public_headers(root):
        include = logical_include(header, root)
        if include in KNOWN_NON_STANDALONE:
            continue
        source = f"#include <{include}>\n"
        result = subprocess.run(
            options + ["-x", "c++", "-fsyntax-only", "-"],
            input=source, text=True, capture_output=True, cwd=directory,
        )
        checked += 1
        if result.returncode != 0:
            failures.append(f"{include}\n{result.stderr.strip()}")

    if failures:
        print(f"Header self-sufficiency FAILED for {len(failures)} of {checked} public headers:\n")
        for failure in failures:
            print(failure + "\n")
        return 1
    print(f"Header self-sufficiency OK: {checked} public headers each compile standalone.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
