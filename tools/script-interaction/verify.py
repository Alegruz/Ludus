"""Execute conformance fixtures; require no-exception code and a VM-free native target."""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys

emulator, executable, database, provider = sys.argv[1:]
root = Path(__file__).resolve().parents[2]
entries = json.loads(Path(database).read_text())
relevant = [entry for entry in entries if any(part in entry["file"] for part in (
    "/modules/runtime/scripting/", "/tools/script-interaction/", "/out/script-interaction/cooked/bindings.cpp",
    "/out/luau-probe/source/VM/src/", "/out/luau-probe/source/Common/src/"))]
assert len(relevant) > 20, "Missing interpreter/runtime compile commands"
for entry in relevant:
    flags = shlex.split(entry["command"])
    assert "-fno-exceptions" in flags and "-fexceptions" not in flags, entry["file"]
    assert not any("LUAU_ENABLE_TIME_TRACE" in flag for flag in flags), entry["file"]
    if "/VM/src/" in entry["file"]:
        assert "-DLUA_USE_LONGJMP=1" in flags, entry["file"]
build = Path(database).parent
commands = subprocess.run([str(root / "out/host-tools/venv/bin/ninja"), "-C", str(build),
                          "-t", "commands", "ludus_script_native"], text=True, check=True, stdout=subprocess.PIPE)
assert "Luau" not in commands.stdout and "ludus_runtime_scripting" not in commands.stdout, "Native fixture pulls a VM"
result = subprocess.run(([emulator] if emulator else []) + [executable],
                        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=110)
print(result.stdout, end="")
assert result.returncode == 0 and "S1 FAIL" not in result.stdout, "S1 fixture failed"
cases = ["explicit-state-before-threshold", "ordered-atomic-command-publication", "phase-checked-application",
         "native-luau-equivalent-effects", "ambiguous-event-order-rejected", "full-identity-checks",
         "command-phase-check", "command-capability-check", "native-numeric-boundaries",
         "queue-capacity-preserves-state-and-effects", "publish-after-complete-preflight",
         "accepted-command-can-fail-at-application", "equivalent-native-luau-fault-policy"]
if provider == "luau":
    cases += ["fault-discards-candidate-and-stops-tick", "invocation-allocation-failure-sweep",
              "retained-state-facade-expires", "runtime-reentrancy-rejected",
              "readonly-config-preserves-candidate", "numeric-and-forged-reference-boundaries",
              "interrupt-preserves-candidate", "restricted-frozen-environment",
              "luau-explicit-rejection-outcomes", "frozen-event-api-result-projections",
              "non-string-error-copy-is-bounded"]
for case in cases:
    assert f"S1 PASS {case}" in result.stdout, "Missing executed case: " + case
binary = Path(executable)
artifacts = [binary] + ([binary.with_suffix(".wasm")] if emulator else [])
evidence = {"provider": provider, "execution": "SDK Node wasm" if emulator else "native",
            "manifest": json.loads((root / "tools/script-interaction/interaction.json").read_text()),
            "cook": json.loads((root / "out/script-interaction/cooked/evidence.json").read_text()),
            "output": result.stdout, "artifacts": {p.name: {"bytes": p.stat().st_size,
                "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in artifacts}}
(binary.parent / f"{provider}-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
