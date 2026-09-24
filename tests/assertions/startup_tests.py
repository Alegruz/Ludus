"""Startup/report-delivery milestone tests.

Exercises the external diagnostic helper + the engine startup integration end to
end, plus direct-launch (no helper) behavior. Nothing here changes or triggers an
assertion's fatal action; this milestone only wires report delivery and the
versioned control handshake.

argv: <helper-binary> <startup-child-binary>

Verified properties:
  * Under the helper (non-CI), the versioned Hello/HelloAck handshake completes,
    reports are drained to the helper's own stdout independently of Logging, and
    the helper exits cleanly after the child (cleanup on child exit).
  * A CI environment forces report-only and never configures the control channel.
  * Direct launch with no helper degrades to report-only with no transport, no
    crash (missing-helper / headless).
  * A malformed handshake ack leaves the control endpoint Failed; reporting still
    works and nothing hangs.
  * Reports are delivered pre-init and post-shutdown and with stderr closed.
  * A full/closed report socket never blocks startup or reporting (bounded core
    transport).
"""

import os
import signal
import socket
import struct
import subprocess
import sys
import threading

# ControlFrame layout: uint32 magic, uint16 version, uint16 type, uint32 pid,
# uint32 payload  ->  "<IHHII" (little-endian, matches x86-64 struct layout).
FRAME = struct.Struct("<IHHII")
MAGIC = 0x4C554443
VERSION = 1
HELLO = 1
HELLO_ACK = 2

CLEAN_ENV = {k: v for k, v in os.environ.items()
             if k not in ("CI", "CONTINUOUS_INTEGRATION", "GITHUB_ACTIONS", "GITLAB_CI",
                          "BUILDKITE", "JENKINS_URL", "TEAMCITY_VERSION", "LUDUS_CI",
                          "LUDUS_DIAGNOSTIC_INTERACTIVE")}


def run_under_helper(helper, child, mode, extra_env=None, timeout=10):
    env = {**CLEAN_ENV}
    if extra_env:
        env.update(extra_env)
    result = subprocess.run([helper, "--", child, mode], capture_output=True,
                            text=True, timeout=timeout, env=env)
    return result


def run_direct(child, mode, extra_env=None, report_fd=None, control_fd=None, timeout=10):
    env = {**CLEAN_ENV}
    if extra_env:
        env.update(extra_env)
    pass_fds = []
    if report_fd is not None:
        env["LUDUS_DIAGNOSTIC_REPORT_FD"] = str(report_fd)
        pass_fds.append(report_fd)
    if control_fd is not None:
        env["LUDUS_DIAGNOSTIC_CONTROL_FD"] = str(control_fd)
        pass_fds.append(control_fd)
    return subprocess.run([child, mode], capture_output=True, text=True,
                          timeout=timeout, env=env, pass_fds=tuple(pass_fds))


def test_helper_end_to_end(helper, child):
    # Non-CI run through the helper. Piped stdio => report-only mode, but the
    # control handshake still completes and reports are drained to helper stdout.
    result = run_under_helper(helper, child, "pre-post")
    assert result.returncode == 0, (result.returncode, result.stderr)
    assert "STARTUP report=1" in result.stdout, result.stdout
    assert "control=1" in result.stdout, ("handshake should complete", result.stdout)
    # Reports were drained to the helper's stdout, not via engine Logging.
    assert "[LUDUS report] pre-init" in result.stdout, result.stdout
    assert "[LUDUS report] post-shutdown" in result.stdout, result.stdout
    assert "PRE=delivered" in result.stdout and "POST=delivered" in result.stdout, result.stdout
    print("  helper end-to-end: handshake + pre/post report delivery OK")


def test_helper_cleanup_on_child_exit(helper, child):
    # The helper must return the child's exit code and not hang after the child
    # exits (cleanup on child exit). Short timeout guards against a hang.
    result = run_under_helper(helper, child, "default", timeout=5)
    assert result.returncode == 0, (result.returncode, result.stderr)
    assert "DELIVERY=delivered" in result.stdout, result.stdout
    print("  helper cleanup on child exit OK")


def test_ci_forces_report_only(helper, child):
    result = run_under_helper(helper, child, "pre-post", extra_env={"CI": "true"})
    assert result.returncode == 0, (result.returncode, result.stderr)
    assert "ci=1" in result.stdout, result.stdout
    assert "control=0" in result.stdout, ("CI must not configure control", result.stdout)
    # Reporting still works under CI.
    assert "[LUDUS report] pre-init" in result.stdout, result.stdout
    print("  CI forces report-only, control endpoint unconfigured OK")


def test_direct_no_helper(child):
    # No descriptors passed: no transport, report-only, no crash.
    result = run_direct(child, "pre-post")
    assert result.returncode == 0, (result.returncode, result.stderr)
    assert "report=0 control=0" in result.stdout, result.stdout
    assert "PRE=unavailable" in result.stdout and "POST=unavailable" in result.stdout, result.stdout
    print("  direct launch without helper: report-only, no crash OK")


def test_malformed_handshake(child):
    # Provide a real control socket but answer with a malformed ack. The engine
    # must mark the control endpoint Failed and keep running (report-only).
    engine_side, driver_side = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    report_recv, report_send = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    got_hello = threading.Event()

    def responder():
        try:
            data = driver_side.recv(4096)
            if len(data) == FRAME.size:
                magic, version, mtype, pid, payload = FRAME.unpack(data)
                if magic == MAGIC and mtype == HELLO:
                    got_hello.set()
                    # Deliberately malformed: wrong magic in the ack.
                    bad = FRAME.pack(0xDEADBEEF, version, HELLO_ACK, pid, 0)
                    driver_side.send(bad)
        except OSError:
            pass

    worker = threading.Thread(target=responder)
    worker.start()
    try:
        result = run_direct(child, "default", report_fd=report_send.fileno(),
                            control_fd=engine_side.fileno(),
                            extra_env={"LUDUS_DIAGNOSTIC_INTERACTIVE": "1"}, timeout=8)
    finally:
        worker.join()
        for s in (engine_side, driver_side, report_recv, report_send):
            s.close()
    assert got_hello.is_set(), "engine should have sent Hello"
    assert result.returncode == 0, (result.returncode, result.stderr)
    assert "control=0" in result.stdout, ("malformed ack => control Failed", result.stdout)
    print("  malformed handshake => control Failed, no hang OK")


def test_stderr_closed(child):
    # Report through the datagram transport with stderr closed; delivery must be
    # unaffected because the assertion transport is not stderr.
    report_recv, report_send = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    packets = []
    report_recv.settimeout(0.2)

    def drain():
        try:
            while True:
                packets.append(report_recv.recv(4096))
        except OSError:
            pass

    worker = threading.Thread(target=drain)
    worker.start()
    try:
        result = run_direct(child, "stderr-closed", report_fd=report_send.fileno(), timeout=5)
    finally:
        worker.join()
        report_recv.close()
        report_send.close()
    assert result.returncode == 0, (result.returncode, result.stderr)
    assert "CLOSED=delivered" in result.stdout, result.stdout
    joined = b"".join(packets).decode("utf-8", "replace")
    assert "stderr-closed" in joined, ("report datagram should arrive", joined)
    print("  stderr-closed: report still delivered via datagram OK")


def test_full_socket_no_block(child):
    # Fill the report socket so sends fail, then confirm startup + reporting do
    # not block (bounded core transport). Short timeout guards a hang.
    report_recv, report_send = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    try:
        try:
            while True:
                report_send.send(b"x" * 2048, socket.MSG_DONTWAIT)
        except BlockingIOError:
            pass
        # Do not drain: the socket stays full. The child must still finish fast.
        result = run_direct(child, "default", report_fd=report_send.fileno(), timeout=5)
        assert result.returncode == 0, (result.returncode, result.stderr)
        assert "report=1" in result.stdout, result.stdout
        # Delivery may be failed (full), but the process must not block.
        assert "DELIVERY=" in result.stdout, result.stdout
    finally:
        report_recv.close()
        report_send.close()
    print("  full report socket: startup/reporting bounded, no block OK")


def main():
    helper = sys.argv[1]
    child = sys.argv[2]
    test_helper_end_to_end(helper, child)
    test_helper_cleanup_on_child_exit(helper, child)
    test_ci_forces_report_only(helper, child)
    test_direct_no_helper(child)
    test_malformed_handshake(child)
    test_stderr_closed(child)
    test_full_socket_no_block(child)
    print("Passed startup/report-delivery: helper handshake, cleanup, CI veto, "
          "missing-helper, malformed handshake, stderr-closed, bounded transport")


if __name__ == "__main__":
    main()
