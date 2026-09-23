"""Fresh native processes; no production behavior overrides or exception tricks."""

from pathlib import Path
import re
import os
import resource
import signal
from transport import run_child
import sys


def main():
    binary = Path(sys.argv[1])
    enabled, check_break = map(int, sys.argv[2:4])
    fake = len(sys.argv) > 4 and sys.argv[4] == "fake"
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

    def run(mode, expected=0):
        result = run_child([str(binary), mode])
        if result.returncode != expected:
            raise AssertionError(f"{mode}: exit {result.returncode}, expected {expected}\n{result.stdout}\n{result.stderr}")
        if any(marker in result.stderr for marker in ("AddressSanitizer", "LeakSanitizer", "ThreadSanitizer", "runtime error:")):
            raise AssertionError(f"{mode}: sanitizer diagnostic\n{result.stderr}")
        if expected != 0:
            for forbidden in ("ATEXIT-RAN", "DESTRUCTOR-RAN", "FATAL-RETURNED", "BAD-SECONDARY-MESSAGE"):
                assert forbidden not in result.stdout, (mode, forbidden)
        return result

    for mode in ("assert", "require", "fatal", "packet"):
        dies = mode != "assert" or enabled
        result = run(mode, -signal.SIGABRT if dies else 0)
        if dies:
            assert f"[LUDUS:{mode.upper() if mode != 'packet' else 'FATAL'}]" in result.stderr
            assert "[LUDUS assertion report v1]" in result.stderr
            assert "assert_child.cpp:" in result.stderr
            assert "function=main" in result.stderr
            assert "FATAL-RETURNED" not in result.stdout
            if mode in ("assert", "require"):
                assert result.stdout.count("CONDITION\n") == 1
                assert result.stdout.count("MESSAGE\n") == 1
                assert result.stderr.count("child-reason") == 1
            if mode == "packet":
                assert "MINIMAL-COMMITTED-BEFORE-MESSAGE" in result.stdout
            if fake:
                # A Check runs before main. Fatal supplies one additional break;
                # continuing that break still reaches the real abort primitive.
                assert result.stdout.count("INSPECTION-CONTINUED") == 1 + check_break
        else:
            assert "CONDITION" not in result.stdout and "MESSAGE" not in result.stdout

    formatted_assert = run("formatted-assert", -signal.SIGABRT if enabled else 0)
    assert formatted_assert.stdout.count("CONDITION\n") == enabled
    assert formatted_assert.stdout.count("MESSAGE\n") == enabled
    malformed = run("formatted-malformed", -signal.SIGABRT)
    assert "[format-error]" in malformed.stderr and "[0]=42" in malformed.stderr
    formatted = run("formatted-require", -signal.SIGABRT)
    assert formatted.stdout.count("CONDITION\n") == 1 and formatted.stdout.count("MESSAGE\n") == 1
    assert "message=child-reason" in formatted.stderr
    run("formatted-fatal", 134)
    run("formatted-check")
    recursive = run("recursive-fatal", 134)
    assert "recursive fatal" in recursive.stderr
    assert "inner-must-not-be-evaluated" not in recursive.stderr
    assert recursive.stdout.count("OUTER-MESSAGE") == 1
    primary = run("primary-secondary", 134)
    assert "secondary fatal" in primary.stderr
    secondary = run("secondary", 134)
    assert "secondary fatal" in secondary.stderr

    for mode in ("recursive-check", "contention", "lifetime"):
        result = run(mode)
        assert "before-main" in result.stderr and "after-main" in result.stderr
        assert "inner-check-must-be-suppressed" not in result.stderr
        assert "contended-message" not in result.stderr

    budget = run("budget")
    assert budget.stderr.count("[LUDUS assertion report v1]") == 64
    assert budget.stderr.count("message=budget-report") == 63
    assert budget.stderr.count("later detail suppressed") == 1
    fatal = run("budget-fatal", -signal.SIGABRT)
    assert "fatal-bypasses-budget" in fatal.stderr

    text = run("text")
    assert "[truncated]" in text.stderr
    assert "line\\x0a\\x09\\x1b\\x5cend" in text.stderr
    metadata = run("metadata")
    assert "expression=LUDUS_TEST_FALSE_TOKEN" in metadata.stderr
    assert "function=main" in metadata.stderr
    assert "literal {} stays literal" in metadata.stderr
    assert re.search(r"site=modules/foundation/base/tests/assert_child.cpp:[0-9]+", metadata.stderr)
    run("closed-output")
    run("barrier")
    identity = run("identity")
    for field in (":987", "expression=", "function=retained-function", "capture=", "message=retained-message", "[truncated]"):
        assert field in identity.stderr, field
    if fake:
        for fault in ("short", "eintr", "eagain", "epipe"):
            os.environ["LUDUS_TEST_SEND_FAULT"] = fault
            run("fatal", -signal.SIGABRT)
            run("lifetime")
        del os.environ["LUDUS_TEST_SEND_FAULT"]
    print(f"Passed subprocess evaluation, fatal, ownership, budget, metadata, and lifetime cases ({binary.name})")


if __name__ == "__main__":
    main()
