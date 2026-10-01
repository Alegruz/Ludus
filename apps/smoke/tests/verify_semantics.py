"""Run the shared smoke app through real wasm callbacks with a controlled provider."""
from pathlib import Path
import subprocess
import sys
node, executable = sys.argv[1:]
for scenario in ("success", "missing-webgpu", "adapter-failure", "device-failure",
                 "surface-failure", "shader-failure", "acquisition-failure",
                 "cancel-startup", "cancel-pipeline", "resize-hidden",
                 "input-suspend", "device-loss", "validation"):
    result = subprocess.run([node, executable, scenario], cwd=Path(executable).parent,
                            capture_output=True, text=True, timeout=10)
    output = result.stdout + result.stderr
    assert result.returncode == 0 and "[W6:passed]" in output, scenario + "\n" + output
    print(output.strip())
