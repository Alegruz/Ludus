"""Failure reporting must retain values, context, failure counts and a failing exit."""
import subprocess
import sys

result = subprocess.run([sys.argv[1], "[test-diagnostics-probe]", "--colour-mode", "none"],
                        capture_output=True, text=True, check=False)
output = result.stdout + result.stderr
for expected in ("17 == 23", "observed := 17", "probe context 17", "view context", "probe failure message", "4 failed"):
    if expected not in output:
        raise SystemExit(f"Missing diagnostic {expected!r}:\n{output}")
if result.returncode != 4:
    raise SystemExit(f"Expected four failing assertions, got exit {result.returncode}:\n{output}")
