"""Check effective compiler policy, rather than the order claimed by CMake comments."""

import json
from pathlib import Path
import shlex
import subprocess
import sys


# Flags that force-include a header/PCH and carry a following path argument.
# When LUDUS_ENABLE_PCH is ON, CMake injects a `-include .../cmake_pch.hxx`
# (and, on some generators, `-include-pch .../cmake_pch.hxx.gch`) into every
# compile command. Replaying such a command for a policy/self-sufficiency probe
# must NOT drag in the precompiled foundational header: it would force-include
# assert_config.hpp (breaking the override-rejection probe) and read a binary
# .gch (breaking UTF-8 decoding). A PCH is a build accelerator only (ADR 0007),
# so these probes strip it and observe the same result with or without the PCH.
_PCH_FLAGS_WITH_ARG = ("-include", "-include-pch", "-include-pch-out")


def compiler_options(entry):
    arguments = entry.get("arguments") or shlex.split(entry["command"])
    result = []
    skip = False
    drop_next = False
    for arg in arguments:
        if skip:
            skip = False
        elif drop_next:
            # The path argument that followed a stripped force-include/PCH flag.
            drop_next = False
        elif arg in ("-o", "-c"):
            skip = True
        elif arg in _PCH_FLAGS_WITH_ARG:
            # Drop the flag and its following path argument (the PCH/header).
            drop_next = True
        # Profiling controls output artifacts, not compiler semantics. Clang
        # rejects -ftime-trace with -fsyntax-only under warnings-as-errors.
        # -fpch-* / -Winvalid-pch tune PCH handling and are meaningless (and can
        # warn) once the -include is gone.
        elif not arg.startswith(("@", "-ftime-trace", "-fpch-", "-Winvalid-pch")):
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
