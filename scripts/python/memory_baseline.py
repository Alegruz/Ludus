"""Run the memory baseline benchmark in fresh processes and record the evidence.

Memory profiling phase 0 (memory-management step 0E). Every workload of
``ludus_memory_bench`` runs once per process, in a seeded random order per
round, so peak RSS is measured per workload rather than for the whole suite.
The runner writes the raw samples (``benchmarks.csv``) and the run metadata,
per-process peak RSS and summaries (``metadata.json``) to an output directory.
See docs/development/memory-phase-0.md for the method and its limits.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import os
import platform
import random
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SAMPLE_FIELDS = ("workload", "sample", "operations", "nanoseconds", "bytes", "checksum")


def parse_samples(text: str) -> list[dict[str, int | str]]:
    """Parse the tool's integer CSV rows; reject malformed output."""
    reader = csv.DictReader(io.StringIO(text))
    if tuple(reader.fieldnames or ()) != SAMPLE_FIELDS:
        raise ValueError(f"unexpected benchmark header: {reader.fieldnames}")
    rows: list[dict[str, int | str]] = []
    for row in reader:
        parsed: dict[str, int | str] = {"workload": row["workload"]}
        for field in SAMPLE_FIELDS[1:]:
            parsed[field] = int(row[field])
        rows.append(parsed)
    return rows


def percentile(values: list[float], fraction: float) -> float:
    """Linear-interpolated percentile of a non-empty list."""
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def summarize(samples: list[dict], runs: list[dict]) -> dict[str, dict[str, float | int]]:
    """Per-workload rate quartiles over every sample and median peak RSS over processes."""
    summary: dict[str, dict[str, float | int]] = {}
    for workload in sorted({str(run["workload"]) for run in runs}):
        rates = [
            float(row["nanoseconds"]) / float(row["operations"])
            for row in samples
            if row["workload"] == workload and int(row["operations"]) > 0
        ]
        rss = [float(run["peak_rss_kib"]) for run in runs if run["workload"] == workload]
        entry: dict[str, float | int] = {"samples": len(rates), "processes": len(rss)}
        if rates:
            entry.update(
                {
                    "ns_per_operation_median": percentile(rates, 0.5),
                    "ns_per_operation_p25": percentile(rates, 0.25),
                    "ns_per_operation_p75": percentile(rates, 0.75),
                    "ns_per_operation_min": min(rates),
                    "ns_per_operation_max": max(rates),
                }
            )
        if rss:
            entry["peak_rss_kib_median"] = percentile(rss, 0.5)
            entry["peak_rss_kib_max"] = max(rss)
        summary[workload] = entry
    return summary


def peak_rss_kib(rusage: object) -> int:
    """ru_maxrss is KiB on Linux and bytes on macOS."""
    value = int(getattr(rusage, "ru_maxrss"))
    return value // 1024 if sys.platform == "darwin" else value


def run_workload(binary: Path, workload: str) -> tuple[str, float, int, int]:
    """Run one workload in a fresh process; return stdout, wall time, peak RSS and exit code."""
    start = time.perf_counter()
    process = subprocess.Popen([str(binary), workload], stdout=subprocess.PIPE)
    output = process.stdout.read() if process.stdout is not None else b""
    _, status, rusage = os.wait4(process.pid, 0)
    wall = time.perf_counter() - start
    exit_code = os.waitstatus_to_exitcode(status)
    process.returncode = exit_code
    if process.stdout is not None:
        process.stdout.close()
    return output.decode("utf-8"), wall, peak_rss_kib(rusage), exit_code


def list_workloads(binary: Path) -> list[str]:
    result = subprocess.run([str(binary), "--list"], check=True, capture_output=True, text=True)
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def read_command(command: list[str]) -> str:
    try:
        return subprocess.run(command, check=True, capture_output=True, text=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unavailable"


def describe_cpu() -> str:
    if sys.platform == "darwin":
        return read_command(["sysctl", "-n", "machdep.cpu.brand_string"])
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.is_file():
        match = re.search(r"^model name\s*:\s*(.+)$", cpuinfo.read_text(encoding="utf-8"), re.MULTILINE)
        if match:
            return match.group(1).strip()
    return platform.processor() or "unavailable"


def read_cmake_cache(cache: Path | None) -> dict[str, str]:
    """Selected compiler/flag entries from the build's CMakeCache.txt."""
    wanted = ("CMAKE_CXX_COMPILER", "CMAKE_BUILD_TYPE", "CMAKE_CXX_FLAGS", "LUDUS_BUILD_FLAVOR", "LUDUS_PROFILING_ENABLED")
    values: dict[str, str] = {}
    if cache is None or cache.is_file() is False:
        return values
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        name, _, rest = line.partition(":")
        if name in wanted and "=" in rest:
            values[name] = rest.split("=", 1)[1]
    compiler = values.get("CMAKE_CXX_COMPILER")
    if compiler:
        values["compiler_version"] = read_command([compiler, "--version"]).splitlines()[0]
    return values


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", type=Path, required=True, help="path to ludus_memory_bench")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--processes", type=int, default=5, help="fresh processes per workload")
    parser.add_argument("--seed", type=int, default=10, help="workload order seed")
    parser.add_argument("--workload", action="append", default=[], help="restrict to a workload (repeatable)")
    parser.add_argument("--cmake-cache", type=Path, help="CMakeCache.txt of the measured build")
    arguments = parser.parse_args(argv)

    binary = arguments.binary.resolve()
    available = list_workloads(binary)
    workloads = arguments.workload or available
    unknown = sorted(set(workloads) - set(available))
    if unknown:
        parser.error(f"unknown workload(s): {', '.join(unknown)}")
    if arguments.processes < 1:
        parser.error("--processes must be at least 1")

    load_before = os.getloadavg()
    generator = random.Random(arguments.seed)
    samples: list[dict] = []
    runs: list[dict] = []
    for process_index in range(arguments.processes):
        order = list(workloads)
        generator.shuffle(order)
        for position, workload in enumerate(order):
            output, wall, rss, exit_code = run_workload(binary, workload)
            runs.append(
                {
                    "workload": workload,
                    "process": process_index,
                    "order": position,
                    "wall_seconds": wall,
                    "peak_rss_kib": rss,
                    "exit_code": exit_code,
                }
            )
            if exit_code != 0:
                print(f"error: {workload} exited with {exit_code}", file=sys.stderr)
                return 1
            for row in parse_samples(output):
                row["process"] = process_index
                row["order"] = position
                samples.append(row)

    summary = summarize(samples, runs)
    output_dir = arguments.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    with (output_dir / "benchmarks.csv").open("w", encoding="utf-8", newline="") as stream:
        fields = ("process", "order", *SAMPLE_FIELDS)
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for row in samples:
            writer.writerow({field: row[field] for field in fields})

    revision = read_command(["git", "-C", str(ROOT), "rev-parse", "HEAD"])
    dirty = read_command(["git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=no"])
    metadata = {
        "revision": revision,
        "working_tree_modified": dirty not in ("", "unavailable"),
        "binary_sha256": sha256(binary),
        "build": read_cmake_cache(arguments.cmake_cache),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "cpu": describe_cpu(),
        "logical_cpus": os.cpu_count(),
        "load_average_1_5_15_before": list(load_before),
        "load_average_1_5_15_after": list(os.getloadavg()),
        "seed": arguments.seed,
        "processes_per_workload": arguments.processes,
        "method": (
            "Each workload runs in a fresh process per round, in seeded random order; one warmup then "
            "seven timed samples per process. Rates are nanoseconds per workload operation; peak RSS is "
            "the process's ru_maxrss. No sanitizer, capture or CPU pinning; other host load is recorded "
            "as load averages, not controlled."
        ),
        "process_runs": runs,
        "summary": summary,
    }
    (output_dir / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")

    print(f"{'workload':28} {'median ns/op':>14} {'p25':>10} {'p75':>10} {'peak RSS KiB':>13}")
    for workload, entry in summary.items():
        print(
            f"{workload:28} {entry.get('ns_per_operation_median', float('nan')):14.3f} "
            f"{entry.get('ns_per_operation_p25', float('nan')):10.3f} "
            f"{entry.get('ns_per_operation_p75', float('nan')):10.3f} "
            f"{entry.get('peak_rss_kib_median', float('nan')):13.0f}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
