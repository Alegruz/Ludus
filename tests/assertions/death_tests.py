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
    # LUDUS_ASSERT_DIALOGS_AVAILABLE for this build (optional 6th arg; default 0).
    # Kept for documentation of the build's dialog eligibility; the fake-backend
    # resume path uses the debugger break, which is not gated by dialogs.
    dialogs = len(sys.argv) > 5 and sys.argv[5] == "1"
    del dialogs
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

    # The runtime CI veto: under CI, ASSERT never breaks or prompts and always
    # terminates, even under the fake attached debugger. Match the runtime's CI
    # detection so the expectation tracks the environment the test runs in.
    under_ci = any(os.environ.get(v) for v in
                   ("CI", "CONTINUOUS_INTEGRATION", "GITHUB_ACTIONS", "GITLAB_CI",
                    "BUILDKITE", "JENKINS_URL", "TEAMCITY_VERSION")) and os.environ.get("LUDUS_CI", "1") != "0"

    # An enabled ASSERT is resumable when an explicit developer action is
    # available. The fake backend simulates an attached debugger, so a debugger
    # Continue resumes past ASSERT (in both Debug and Development) unless vetoed by
    # CI. The native backend has no debugger and the test transport supplies no
    # control endpoint, so ASSERT terminates. REQUIRE/FATAL always terminate.
    assert_resumes = enabled and fake and not under_ci

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
        # REQUIRE/FATAL/packet always terminate. ASSERT terminates unless it can
        # resume via the fake debugger Continue in a dialog-eligible build.
        dies = mode != "assert" or (enabled and not assert_resumes)
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
                # A Check runs before main (its break is gated by check_break AND
                # suppressed under CI). REQUIRE/FATAL/packet break once more before
                # aborting; a terminating ASSERT (here only under the CI veto) is
                # report-only and never breaks.
                before_main_break = check_break if not under_ci else 0
                fatal_break = 0 if mode == "assert" else 1
                assert result.stdout.count("INSPECTION-CONTINUED") == fatal_break + before_main_break
        elif mode == "assert" and assert_resumes:
            # Resumed: condition + message ran once, ASSERT returned to caller,
            # main returned normally so atexit/destructors ran. Under the fake
            # debugger, the ASSERT break plus the before-main and after-main
            # CHECK breaks (each gated by check_break) all continue.
            assert "ASSERT-RETURNED" in result.stdout, result.stdout
            assert result.stdout.count("CONDITION\n") == 1
            assert result.stdout.count("MESSAGE\n") == 1
            assert result.stdout.count("INSPECTION-CONTINUED") == 1 + 2 * check_break
        else:
            assert "CONDITION" not in result.stdout and "MESSAGE" not in result.stdout

    if assert_resumes:
        # A second ASSERT after a resumed one must be independently reportable.
        # Two ASSERT breaks continue, plus the before/after-main CHECK breaks.
        repeated = run("assert-repeated", 0)
        assert repeated.stdout.count("INSPECTION-CONTINUED") == 2 + 2 * check_break, repeated.stdout
        assert "ASSERT-RETURNED" in repeated.stdout

    formatted_assert = run("formatted-assert", 0 if (not enabled or assert_resumes) else -signal.SIGABRT)
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
