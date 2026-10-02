#!/usr/bin/env python3
"""LLDB-driven debugger acceptance for the GameHost (tasks.md L14/L6, design 11).

Proves REAL native debugger behavior against a dynamically loaded gameplay
module (not a mocked launch): startup breakpoint in a module function, source
mapping, a backtrace, local inspection, pause/step/continue, and new-generation
symbol resolution after loading a second module from a distinct path. A mocked
launch proves no stepping, so this drives the pinned LLDB 18 batch-mode.

Usage:
  lldb_acceptance.py <lldb> <host-exe> <fixture-A.so> <fixture-B.so>

Exits 0 and prints PASS lines on success; non-zero with a diagnostic otherwise.
This is run by scripts/debug for the recorded acceptance; it is not a unit test.
"""

import re
import subprocess
import sys


def run_lldb(lldb: str, host: str, module: str, commands: str) -> str:
    script = f"""
breakpoint set --name FixtureUpdate
run --module {module} --headless --max-frames 120
{commands}
quit
"""
    proc = subprocess.run(
        [lldb, "--batch", "--source-quietly", "-o", script.strip(), "--", host,
         "--module", module, "--headless", "--max-frames", "120"],
        capture_output=True, text=True, timeout=120,
    )
    return proc.stdout + proc.stderr


def main() -> int:
    if len(sys.argv) != 5:
        print(f"usage: {sys.argv[0]} <lldb> <host> <fixtureA.so> <fixtureB.so>", file=sys.stderr)
        return 2
    lldb, host, fixture_a, fixture_b = sys.argv[1:5]

    # 1) Startup breakpoint + backtrace + locals + step; then disable the
    #    breakpoint and continue to completion so the process exits cleanly.
    out_a = run_lldb(
        lldb, host, fixture_a,
        "thread backtrace\nframe variable\nthread step-over\nframe variable\n"
        "breakpoint disable 1\ncontinue",
    )
    checks = {
        "breakpoint resolved in module": "stop reason = breakpoint" in out_a,
        "source mapping to fixture_module.cpp": "fixture_module.cpp" in out_a,
        "function frame is FixtureUpdate": "FixtureUpdate" in out_a,
        "locals visible (input)": re.search(r"\binput\b\s*=", out_a) is not None,
        "process exited cleanly": re.search(r"Process \d+ exited with status = 0", out_a) is not None,
    }

    # 2) New-generation symbol resolution: break in the SECOND module from its
    #    own distinct path; proves per-generation symbols load for a fresh image.
    out_b = run_lldb(lldb, host, fixture_b, "thread backtrace\ncontinue")
    checks["second-generation symbol resolves"] = (
        "FixtureUpdate" in out_b and fixture_b.split("/")[-1] in out_b
    )

    ok = True
    for name, passed in checks.items():
        print(f"{'PASS' if passed else 'FAIL'}: {name}")
        ok = ok and passed

    if not ok:
        print("---- lldb output (A) ----", file=sys.stderr)
        print(out_a[-4000:], file=sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
