"""Require actual execution and an exception-free, interpreter-only compile profile."""
from pathlib import Path
import hashlib
import json
import platform
import shlex
import subprocess
import sys

emulator, executable, database = sys.argv[1:]
entries = json.loads(Path(database).read_text())
relevant = [e for e in entries if "/tools/luau-probe/main.cpp" in e["file"] or
            "/out/luau-probe/source/VM/src/" in e["file"] or
            "/out/luau-probe/source/Common/src/" in e["file"]]
assert len(relevant) > 20, "VM compile commands missing"
for entry in relevant:
    flags = shlex.split(entry["command"])
    assert "-fno-exceptions" in flags and "-fexceptions" not in flags, entry["file"]
    if "/VM/src/" in entry["file"]:
        assert "-DLUA_USE_LONGJMP=1" in flags, entry["file"]
    assert not any("LUAU_ENABLE_TIME_TRACE" in f for f in flags), entry["file"]
command = ([emulator] if emulator else []) + [executable]
result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=110)
print(result.stdout, end="")
assert result.returncode == 0, f"Probe exited {result.returncode}"
for case in ("native-binding-and-library-profile", "protected-script-error", "protected-binding-error",
             "protected-stack-overflow", "bounded-loop-interrupt", "constant-loader-oom-sweep",
             "execution-oom-sweep", "break-step-locals-node-resume", "dispatch-batch", "fresh-vm-recovery"):
    assert f"S0 PASS {case}" in result.stdout, f"Missing executed case: {case}"
assert "S0 FAIL" not in result.stdout
assert "allocation-points=" in result.stdout and "heap-peak=" in result.stdout
binary = Path(executable)
artifacts = [binary] + ([binary.with_suffix('.wasm')] if emulator else [])
evidence = {
    "execution": "SDK Node wasm" if emulator else "native",
    "host": platform.platform(),
    "pin": json.loads((Path(__file__).resolve().parents[2] / "config/luau_toolchain.json").read_text()),
    "compile_policy": "VM, Common and host trampoline: no C++ exceptions; VM longjmp=1",
    "artifacts": {p.name: {"bytes": p.stat().st_size, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                  for p in artifacts},
    "runtime_output": result.stdout,
    "scope": "private feasibility probe, not production API or platform acceptance",
}
(binary.parent / "evidence.json").write_text(json.dumps(evidence, indent=2) + '\n')
