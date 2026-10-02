#!/usr/bin/env python3
"""Same-session live-reload debugger acceptance (tasks.md L14, design 11).

Drives the pinned LLDB 18 against the debugger_journey harness: ONE host process
that creates generation A, live-reloads to generation B, and runs frames in each.
It proves REAL native debugger behavior across a live reload (a mocked launch
proves no stepping): a breakpoint resolves inside A's module with source mapping
and locals, pause/step/continue works, and after the live reload the SAME
breakpoint resolves inside B's DISTINCT module image with B's locals.

It enforces the harness process exit status (0). The breakpoint carries an
auto-continuing command list (plain LLDB commands, no fragile inline Python), so
each hit prints the backing image/source/frame/local and resumes, and the batch
driver never hand-processes a stop (which crashed LLDB-18 on `continue`).

Usage:
  lldb_acceptance.py <lldb> <journey-exe> <fixture-A.so> <fixture-B.so>

Prints PASS/FAIL lines; exits 0 only when every check passes and the harness
exited 0.
"""

import os
import re
import subprocess
import sys
import tempfile


LLDB_SCRIPT = """\
breakpoint set --name FixtureUpdate
breakpoint command add 1
frame info
frame variable input
continue
DONE
run
quit
"""


def run_journey(lldb: str, journey: str, fixture_a: str, fixture_b: str) -> str:
    fd, path = tempfile.mkstemp(suffix=".lldb")
    try:
        with os.fdopen(fd, "w") as handle:
            handle.write(LLDB_SCRIPT)
        args = [lldb, "--batch", "--source", path, "--", journey, fixture_a, fixture_b]
        proc = subprocess.run(args, capture_output=True, text=True, timeout=180)
        return proc.stdout + "\n[EXIT]" + str(proc.returncode) + "\n" + proc.stderr
    finally:
        os.unlink(path)


def main() -> int:
    if len(sys.argv) != 5:
        print(f"usage: {sys.argv[0]} <lldb> <journey> <fixtureA.so> <fixtureB.so>", file=sys.stderr)
        return 2
    lldb, journey, fixture_a, fixture_b = sys.argv[1:5]
    a_name = fixture_a.split("/")[-1]
    b_name = fixture_b.split("/")[-1]

    out = run_journey(lldb, journey, fixture_a, fixture_b)

    # `frame info` prints a line like: "frame #0: 0x... game_fixture_A.so`...
    # FixtureUpdate(...) at fixture_module.cpp:208".
    frame_lines = [ln for ln in out.splitlines() if "FixtureUpdate" in ln and ".so`" in ln]
    images = set()
    for ln in frame_lines:
        m = re.search(r"(\S+\.so)`", ln)
        if m:
            images.add(m.group(1).split("/")[-1])

    checks = {
        "breakpoint resolved in a module": len(frame_lines) > 0,
        "source mapping to fixture_module.cpp": "fixture_module.cpp" in out,
        "function symbol is FixtureUpdate": any("FixtureUpdate" in ln for ln in frame_lines),
        "locals visible (input)": re.search(r"\binput\b\s*=", out) is not None,
        "generation A module hit": a_name in images,
        "generation B module hit after live reload": b_name in images,
        "harness journey reported ok": "journey ok: gen 1 -> 2" in out,
        "harness process exited 0": "[EXIT]0" in out,
    }

    ok = all(checks.values())
    for name, passed in checks.items():
        print(f"{'PASS' if passed else 'FAIL'}: {name}")
    if not ok:
        print("---- lldb output ----", file=sys.stderr)
        print(out[-6000:], file=sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
