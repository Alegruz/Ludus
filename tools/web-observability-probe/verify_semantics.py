"""Verify wasm logging failure recovery and trace export/compile-out."""
import json
from pathlib import Path
import subprocess
import sys

node, executable, enabled = sys.argv[1:]
result = subprocess.run([node, executable], cwd=Path(executable).parent,
                        capture_output=True, text=True, timeout=20)
output = result.stdout + result.stderr
assert result.returncode == 0 and "[W2:passed]" in output, output
assert "formatted 42 1.25" in output and "console recovered" in output, output
assert "[LUDUS:CHECK]" in output and "W2 logger-independent CHECK" in output, output
if enabled == "1":
    trace_text = result.stdout.split("[W2:trace]", 1)[1]
    trace, _ = json.JSONDecoder().raw_decode(trace_text)
    events = trace["traceEvents"]
    assert len(events) == 5 and events[0]["ph"] == "B" and events[-1]["ph"] == "E", events
    assert {event["tid"] for event in events} == {0}, events
    assert all(a["ts"] <= b["ts"] for a, b in zip(events, events[1:])), events
else:
    assert "[W2:trace]" not in output, output
print("W2 logging and profiling semantics verified")
