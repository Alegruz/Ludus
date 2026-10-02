"""Run the FoundationMath wasm numeric corpus through the SDK Node runtime and
assert it reports zero failures. The probe prints a single summary line; a
nonzero process exit or a missing/failed summary is a hard failure.
"""
from pathlib import Path
import subprocess
import sys

node, executable = sys.argv[1:3]
result = subprocess.run([node, executable], cwd=Path(executable).parent,
                        capture_output=True, text=True, timeout=60)
output = result.stdout + result.stderr
assert "LUDUS_WEB_MATH_RESULT failures=0" in output, output
assert result.returncode == 0, output
print("web math corpus: 0 failures verified")
