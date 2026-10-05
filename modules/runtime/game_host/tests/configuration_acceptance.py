"""Exercise configured startup through the real host and real gameplay module."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

host, module = sys.argv[1:]
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "project.json"
    preference = Path(directory) / "preference.json"
    def write(destination, layer, frames):
        destination.write_text(json.dumps({"version": 1, "schema": "ludus.host.v1", "layer": layer, "values": [
            {"key": "host.headless", "type": "bool", "value": True, "source": "test", "line": 0},
            {"key": "host.max_frames", "type": "uint64", "value": str(frames), "source": "test", "line": 0}]}), encoding="utf-8")
    write(path, "project", 1)
    write(preference, "preference", 0)  # Unlimited: CLI must win or timeout.
    for flags in (["--config", str(path)], ["--max-frames", "2", "--preferences", str(preference), "--config", str(path)]):
        result = subprocess.run([host, "--module", module, *flags], capture_output=True, timeout=15)
        assert result.returncode == 0, result.stderr
    path.write_text("{}", encoding="utf-8")
    result = subprocess.run([host, "--module", module, "--headless", "--max-frames", "1", "--config", str(path)], capture_output=True, timeout=15)
    assert result.returncode != 0, "malformed file was ignored"
