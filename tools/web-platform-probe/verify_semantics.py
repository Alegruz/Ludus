"""Run the real wasm backend with an explicit deterministic Node DOM fixture."""
from pathlib import Path
import subprocess
import sys
node, executable = sys.argv[1:]
result = subprocess.run([node, executable, "automated"], cwd=Path(executable).parent,
                        capture_output=True, text=True, timeout=20)
output = result.stdout + result.stderr
assert result.returncode == 0 and "[W3:passed]" in output, output
print("W3 canvas/input lifecycle semantics verified (Node DOM fixture)")
