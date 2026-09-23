"""Check effective compiler policy, rather than the order claimed by CMake comments."""

import json
from pathlib import Path
import shlex
import subprocess
import sys


def compiler_options(entry):
    arguments = entry.get("arguments") or shlex.split(entry["command"])
    result = []
    skip = False
    for arg in arguments:
        if skip:
            skip = False
        elif arg in ("-o", "-c"):
            skip = True
        # Profiling controls output artifacts, not compiler semantics. Clang
        # rejects -ftime-trace with -fsyntax-only under warnings-as-errors.
        elif not arg.startswith(("@", "-ftime-trace")):
            result.append(arg)
    return result


def main():
    entries = json.loads(Path(sys.argv[1]).read_text())
    checked = 0
    for entry in entries:
        # Catch2 executables follow the *_tests target convention. Native
        # assertion child executables intentionally do not opt into exceptions.
        command = entry.get("command") or " ".join(entry["arguments"])
        if "CMakeFiles/ludus_" not in command:
            continue
        expected = "_tests.dir/" in command
        result = subprocess.run(
            compiler_options(entry) + ["-x", "c++", "-dM", "-E", "-"],
            input="", text=True, capture_output=True, cwd=entry["directory"], check=True,
        )
        actual = "#define __cpp_exceptions " in result.stdout
        if actual != expected:
            raise RuntimeError(f"Wrong exception policy: {entry['file']}: {actual}, expected {expected}")
        checked += 1
    if not checked:
        raise RuntimeError("No Ludus compilation commands checked")
    print(f"Verified effective exception policy for {checked} compilation commands")


if __name__ == "__main__":
    main()
