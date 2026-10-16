"""Verify that unsafe UniquePtr uses fail to compile with actionable diagnostics.

docs/architecture/smart-pointers.md (slice 1, "Unique correctness") requires
negative compilation for throwing/mismatched deleters and unsafe ownership
conversions, and an actionable diagnostic when the ordinary default deleter is
instantiated for an incomplete type. Constraint predicates are also checked by
static_asserts in modules/foundation/base/tests/pointer_tests.cpp; this gate
proves the corresponding real uses are rejected by the compiler.

Each case in modules/foundation/base/tests/pointer_compile_failures.cpp is
compiled with -fsyntax-only using the flags of FoundationBase's version.cpp from
compile_commands.json. The file must compile without a case (control run), and
every case must fail with output matching its expected pattern.

Usage: verify_pointer_diagnostics.py <build_dir>
"""

import json
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_compile_policy import compiler_options, is_pch_entry


SOURCE = "modules/foundation/base/tests/pointer_compile_failures.cpp"
CASES: dict[int, tuple[str, str]] = {
    1: ("incomplete default deletion", r"DefaultDeleter requires a complete type"),
    2: ("throwing deleter", r"constraints not satisfied|does not satisfy 'Deleter'"),
    3: ("non-virtual base conversion", r"no matching (constructor|conversion)|no viable conversion"),
    4: ("stateful bare release", r"Release.*(constraints not satisfied|no matching member)|"
                                 r"invalid reference to function 'Release'"),
    5: ("array element type", r"constraints not satisfied|is_array_v"),
}


def representative_entry(entries):
    for entry in entries:
        if entry["file"].endswith("foundation/base/src/version.cpp") and not is_pch_entry(entry):
            return entry
    return None


def compile_case(options: list[str], directory: str, source: Path, case: int | None):
    define = [] if case is None else [f"-DLUDUS_POINTER_CASE={case}"]
    return subprocess.run(options + define + ["-x", "c++", "-fsyntax-only", str(source)],
                          text=True, capture_output=True, cwd=directory)


def main() -> int:
    build = Path(sys.argv[1]).resolve()
    entries = json.loads((build / "compile_commands.json").read_text())
    entry = representative_entry(entries)
    if entry is None:
        print("FoundationBase version.cpp is missing from compile_commands.json")
        return 1
    root = Path(__file__).resolve().parents[2]
    source = root / SOURCE
    options = compiler_options(entry)

    control = compile_case(options, entry["directory"], source, None)
    if control.returncode != 0:
        print(f"Control compilation of {SOURCE} failed; the replayed flags are invalid:\n{control.stderr}")
        return 1

    failures: list[str] = []
    for case, (name, pattern) in CASES.items():
        result = compile_case(options, entry["directory"], source, case)
        output = result.stdout + result.stderr
        if result.returncode == 0:
            failures.append(f"case {case} ({name}) compiled but must be rejected")
        elif re.search(pattern, output) is None:
            failures.append(f"case {case} ({name}) failed without the expected diagnostic /{pattern}/:\n{output}")

    if failures:
        print("UniquePtr negative compilation FAILED:\n")
        for failure in failures:
            print(failure + "\n")
        return 1
    print(f"UniquePtr negative compilation OK: {len(CASES)} unsafe uses rejected with expected diagnostics.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
