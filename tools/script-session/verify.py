"""Run the same acceptance contract natively and in Node/Wasm."""
import subprocess
import sys
command = ([sys.argv[1]] if sys.argv[1] else []) + [sys.argv[2]]
result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=90)
print(result.stdout)
if result.returncode or "S2 ACCEPTANCE PASS" not in result.stdout:
    raise SystemExit("S2 acceptance failed")
