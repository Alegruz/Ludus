"""Require real execution, an exception-free compile profile and size evidence for L0."""
from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import gzip
import hashlib
import os
import json
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

CASES = ("embedded-data-registration", "pinned-icu-cldr-unicode-versions", "canonical-locales-with-sliced-data",
         "profile-accepts-every-corpus-pattern", "profile-rejects-every-negative-fixture",
         "corpus-format-and-argument-validation", "complete-output-or-unchanged-caller-bytes",
         "backend-observations-require-adapter-validation", "sweep-allocation-points",
         "dynamic-format-measurements", "host-cleanup-releases-every-allocation")
SWEEPS = ("nested-select-plural", "number-skeleton", "offset-plural-text", "host-initialization")
ROOT = Path(__file__).resolve().parents[2]


def compressed(data: bytes) -> int:
    return len(gzip.compress(data, compresslevel=9, mtime=0))


def artifact(path: Path) -> dict:
    data = path.read_bytes()
    result = {"bytes": len(data), "gzip_bytes": compressed(data), "sha256": hashlib.sha256(data).hexdigest()}
    strip = shutil.which("llvm-strip-18") or shutil.which("strip")
    if path.suffix not in (".js", ".wasm") and strip is not None:
        with tempfile.TemporaryDirectory() as directory:
            copy = Path(directory) / path.name
            shutil.copyfile(path, copy)
            if subprocess.run([strip, str(copy)], capture_output=True).returncode == 0:
                stripped = copy.read_bytes()
                result.update(stripped_bytes=len(stripped), stripped_gzip_bytes=compressed(stripped))
    return result


def run_point(command: list[str], index: int, fail_at: int) -> dict:
    """Run one fail point in a fresh process; a crash is data, not a verifier error."""
    try:
        result = subprocess.run(command + ["sweep", str(index), str(fail_at)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return {"fail_at": fail_at, "outcome": "timeout"}
    match = re.search(r"L0 SWEEP (.*)$", result.stdout, re.MULTILINE)
    if result.returncode != 0 or match is None:
        return {"fail_at": fail_at, "outcome": "crash", "exit": result.returncode}
    fields = dict(pair.split("=", 1) for pair in match.group(1).split())
    fields["fail_at"] = fail_at
    if fields["denied"] == "0":
        fields["outcome"] = "control"
    elif fields["initialized"] == "false":
        fields["outcome"] = "reported-initialization-failure"
    elif fields["correct"] == "true":
        fields["outcome"] = "absorbed"
    elif fields["status"] == "Ok":
        fields["outcome"] = "wrong-output"
    elif fields["status"] == "OutOfMemory":
        fields["outcome"] = "reported-oom"
    else:
        fields["outcome"] = "other-error"
    return fields


def sweep(command: list[str], points: dict[str, int]) -> dict:
    """Deny each allocation once, in isolation, for every representative case."""
    report = {}
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
        for index, name in enumerate(SWEEPS):
            count = points[name]
            results = list(pool.map(lambda f, i=index: run_point(command, i, f), range(1, count + 2)))
            control = results[-1]
            assert control["outcome"] == "control" and control["correct"] == "true" and \
                control["leaked-blocks"] == "0", f"{name}: control point is not clean: {control}"
            failures = results[:-1]
            summary = {"allocation_points": count}
            for outcome in ("crash", "timeout", "reported-initialization-failure", "reported-oom", "other-error",
                            "absorbed", "wrong-output", "control"):
                summary[outcome] = sum(r["outcome"] == outcome for r in failures)
            survived = [r for r in failures if r["outcome"] not in ("crash", "timeout")]
            # A reported initialization failure has no formatter to recover.
            formatted = [r for r in survived if r["outcome"] != "reported-initialization-failure"]
            summary["sticky_after_failure"] = sum(r["recovered"] == "false" for r in formatted)
            summary["leaking_after_cleanup"] = sum(r["leaked-blocks"] != "0" for r in survived)
            summary["misreported_backend_codes"] = sorted({r["backend"] for r in survived
                                                           if r["outcome"] == "other-error"})
            summary["crash_points"] = [r["fail_at"] for r in failures if r["outcome"] == "crash"]
            summary["wrong_output_points"] = [r["fail_at"] for r in failures if r["outcome"] == "wrong-output"]
            assert summary["control"] == 0, f"{name}: allocation count changed between runs"
            report[name] = summary
            print(f"L0 SWEEP SUMMARY {name} " + json.dumps(summary))
    return report


def main() -> int:
    emulator, executable, baseline, database = sys.argv[1:]
    entries = json.loads(Path(database).read_text())
    relevant = [e for e in entries if "/tools/icu-probe/" in e["file"] or "/out/icu-probe/" in e["file"]]
    upstream = [e for e in relevant if "/out/icu-probe/source/" in e["file"]]
    assert len(upstream) > 400, f"ICU compile commands missing ({len(upstream)})"
    for entry in relevant:
        flags = shlex.split(entry["command"]) if "command" in entry else entry["arguments"]
        assert "-fno-exceptions" in flags and "-fexceptions" not in flags, entry["file"]
        if "/out/icu-probe/source/" in entry["file"]:
            assert "-DUCONFIG_NO_MF2=1" in flags and "-DUCONFIG_NO_FILE_IO=1" in flags, entry["file"]
    command = ([emulator] if emulator else []) + [executable]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=580)
    print(result.stdout, end="")
    assert result.returncode == 0, f"Probe exited {result.returncode}"
    for case in CASES:
        assert f"L0 PASS {case}" in result.stdout, f"Missing executed case: {case}"
    assert "L0 FAIL" not in result.stdout
    metrics = {}
    for line in result.stdout.splitlines():
        match = re.search(r"L0 METRIC (\S+) (.*)$", line)
        if match:
            metrics[match.group(1)] = dict(pair.split("=", 1) for pair in match.group(2).split())
    observations = [line.split("L0 OBSERVED ", 1)[1] for line in result.stdout.splitlines() if "L0 OBSERVED " in line]
    points = {}
    for line in result.stdout.splitlines():
        match = re.search(r"L0 METRIC sweep-points-(\S+) allocations=(\d+)", line)
        if match:
            points[match.group(1)] = int(match.group(2))
    assert set(points) == set(SWEEPS), f"Missing sweep allocation counts: {points}"
    sweeps = sweep(command, points)
    probe, reference = Path(executable), Path(baseline)
    artifacts = {"probe": artifact(probe), "baseline": artifact(reference)}
    if emulator:
        artifacts["probe_wasm"] = artifact(probe.with_suffix(".wasm"))
        artifacts["baseline_wasm"] = artifact(reference.with_suffix(".wasm"))
    libraries = {}
    for library in sorted(probe.parent.glob("*ludus_icu_probe_*.a")):
        libraries[library.name] = library.stat().st_size
    data = ROOT / "out/icu-probe/data/manifest.json"
    pin = json.loads((ROOT / "config/icu_toolchain.json").read_text())
    evidence = {
        "execution": "SDK Node wasm" if emulator else "native",
        "host": platform.platform(),
        "icu": pin["version"],
        "profile": pin["profile"],
        "configuration": pin["configuration"],
        "data": {key: value for key, value in json.loads(data.read_text()).items() if key != "items"},
        "data_items": len(json.loads(data.read_text())["items"]),
        "artifacts": artifacts,
        "static_libraries": libraries,
        "metrics": metrics,
        "observations": observations,
        "allocation_failure_sweep": sweeps,
        "scope": "private feasibility probe, not production API or platform acceptance",
    }
    (probe.parent / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps({"artifacts": artifacts, "static_libraries": libraries}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
