"""Failure reporting must retain values, context, failure counts and a failing exit."""
import subprocess
import sys

result = subprocess.run([sys.argv[1], "[test-diagnostics-probe]", "--colour-mode", "none"],
                        capture_output=True, text=True, check=False)
output = result.stdout + result.stderr
for expected in ("17 == 23", "observed := 17", "probe context 17", "probe failure message", "3 failed"):
    if expected not in output:
        raise SystemExit(f"Missing diagnostic {expected!r}:\n{output}")
if result.returncode != 3:
    raise SystemExit(f"Expected three failing assertions, got exit {result.returncode}:\n{output}")
