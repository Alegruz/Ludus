"""Require clean startup and a fatal, specific finding from a test-only child."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import tempfile

from quality_process import LOG_BYTES, LogBudget, run_bounded


DIAGNOSTICS = {
    "asan": "ERROR: AddressSanitizer: heap-buffer-overflow",
    "ubsan": "runtime error: signed integer overflow",
    "tsan": "WARNING: ThreadSanitizer: data race",
}


def verify(kind: str, child: Path, output: Path, symbolizer: str) -> bool:
    environment = dict(os.environ)
    # Do not inherit suppressions, log redirection, or recovery from a developer.
    environment["ASAN_OPTIONS"] = "halt_on_error=1:detect_leaks=0:symbolize=1"
    # Prove the compiler's fatal policy even when runtime recovery is requested.
    environment["UBSAN_OPTIONS"] = "halt_on_error=0:print_stacktrace=1:symbolize=1"
    environment["TSAN_OPTIONS"] = "halt_on_error=1:exitcode=66:symbolize=1"
    if symbolizer != "none":
        if not Path(symbolizer).is_file():
            raise ValueError(f"Missing sanitizer symbolizer: {symbolizer}")
        for name in ("ASAN_SYMBOLIZER_PATH", "UBSAN_SYMBOLIZER_PATH", "TSAN_SYMBOLIZER_PATH"):
            environment[name] = symbolizer
    output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    budget = LogBudget(2 * LOG_BYTES)
    results = []
    for mode, arguments in (("clean", []), ("fault", ["fault"])):
        result = run_bounded([child, *arguments], cwd=directory, env=environment,
                             log=directory / f"{mode}.log", timeout=15, budget=budget)
        text = result.log.read_text(encoding="utf-8", errors="replace")
        passed = result.complete and (result.returncode == 0 if mode == "clean" else
                                     result.returncode != 0 and DIAGNOSTICS[kind] in text)
        results.append({"mode": mode, "passed": passed, "exit_code": result.returncode,
                        "log": result.log.name, "truncated": result.truncated,
                        "timed_out": result.timed_out})
        print(f"{kind} {mode}: {'pass' if passed else 'FAIL'}; exit={result.returncode}")
        print(text, end="", flush=True)
    passed = all(result["passed"] for result in results)
    (directory / "results.json").write_text(json.dumps(
        {"version": 1, "kind": kind, "child": str(child), "passed": passed, "results": results},
        indent=2) + "\n")
    print(f"Sanitizer control evidence: {directory}")
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=DIAGNOSTICS, required=True)
    parser.add_argument("--child", type=Path, required=True)
    parser.add_argument("--symbolizer", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        return 0 if verify(args.kind, args.child.resolve(), args.output.resolve(), args.symbolizer) else 1
    except (OSError, ValueError) as error:
        print(f"Sanitizer control failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
