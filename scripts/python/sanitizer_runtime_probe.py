"""Require a trivial sanitized program to start and exit within a bound.

CMake's execute_process(TIMEOUT) reports a timeout without killing the child,
so a deadlocked sanitizer runtime would outlive configuration. run_bounded owns
the child's process group and kills it on timeout.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import signal
import sys
from typing import Sequence

from quality_process import run_bounded


# Exit statuses distinguish a hang from a crash or nonzero exit for CMake.
EXIT_TIMED_OUT = 124
EXIT_FAILED = 1


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--child", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--timeout", type=float, required=True, help="seconds before the child is killed")
    args = parser.parse_args(argv)
    args.log.unlink(missing_ok=True)
    result = run_bounded([args.child], cwd=args.child.parent, log=args.log, timeout=args.timeout)
    sys.stdout.write(result.log.read_text(encoding="utf-8", errors="replace"))
    if result.timed_out:
        print(f"timed out after {args.timeout:g} s; the probe process group was killed")
        return EXIT_TIMED_OUT
    if result.returncode != 0:
        status = f"exit status {result.returncode}"
        if -result.returncode in signal.valid_signals():
            # Negative return codes are the POSIX signal that ended the child.
            status = f"terminated by {signal.Signals(-result.returncode).name}"
        print(status)
        return EXIT_FAILED
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
