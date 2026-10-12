"""Audio packaging rejects mesh roots before file access and preserves output."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    source = root / "content"
    output = root / "output"
    source.mkdir()
    output.mkdir()
    (source / "catalog.json").write_text(json.dumps({"version": 1, "resources": [
        {"id": "mesh/quad", "kind": "mesh", "path": "meshes/quad"}
    ]}))
    plan = output / "closure.json"
    plan.write_bytes(b"preserved previous plan")
    result = subprocess.run([sys.argv[1], str(source), str(output), "mesh/quad"],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 1, result
    assert "mesh roots are unsupported" in result.stdout + result.stderr, result
    assert plan.read_bytes() == b"preserved previous plan"
