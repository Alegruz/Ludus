"""Run separate wasm instances to verify continuing versus terminal failures."""
from pathlib import Path
import subprocess
import sys

node, executable, enabled = sys.argv[1:]
for mode in ("normal", "check", "console-failure", "assert", "require", "fatal", "trap"):
    result = subprocess.run([node, executable, mode], cwd=Path(executable).parent,
                            capture_output=True, text=True, timeout=20)
    output = result.stdout + result.stderr
    terminal = mode in ("require", "fatal", "trap") or (mode == "assert" and enabled == "1")
    if terminal:
        assert result.returncode != 0 and "[W1:after]" not in output, (mode, output)
        if mode != "trap":
            assert f"[LUDUS:{mode.upper()}]" in output, (mode, output)
    else:
        assert result.returncode == 0 and "[W1:after]" in output, (mode, output)
        if mode == "check":
            assert output.count("[LUDUS:CHECK]") == 2 and "value=42 float=1.25" in output, output
        if mode == "assert":
            assert "[LUDUS:ASSERT]" not in output, output
    print(f"{mode}: {'terminal' if terminal else 'returned'} verified")
