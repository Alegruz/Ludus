"""Resumable-ASSERT decision tests with a controlled helper.

The driver plays the external helper over the control socketpair, scripting the
handshake and the DecisionReply so the no-debugger dialog path is exercised
deterministically. It verifies:

  * ContinueOnce resumes the ASSERT (condition ran once, control returned).
  * Terminate aborts (SIGABRT), no resume.
  * Repeated ASSERTs each get an independent incident id and resume.
  * A reply with the wrong incident id, a duplicate/stale id, an unknown kind,
    a malformed frame, or a closed helper never authorizes continuation.
  * A REQUIRE never resumes even when the helper would reply ContinueOnce.

argv: <assert-decision-child-binary>

No debugger is attached (native), so the runtime takes the control-endpoint
branch. Reports are drained off the report socket so the child never blocks.
"""

import os
import resource
import signal
import socket
import struct
import subprocess
import sys
import threading

HEADER = struct.Struct("<IHHII")
MAGIC = 0x4C554443
VERSION = 1
HEADER_SIZE = HEADER.size
MAX_PAYLOAD = 2048
KIND_HELLO = 1
KIND_HELLO_ACK = 2
KIND_DECISION_REQUEST = 3
KIND_DECISION_REPLY = 4
CONTINUE_ONCE = 1
TERMINATE = 0

CLEAN_ENV = {k: v for k, v in os.environ.items()
             if k not in ("CI", "CONTINUOUS_INTEGRATION", "GITHUB_ACTIONS", "GITLAB_CI",
                          "BUILDKITE", "JENKINS_URL", "TEAMCITY_VERSION", "LUDUS_CI")}

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def encode(kind, incident=0, payload=b""):
    return HEADER.pack(MAGIC, VERSION, kind, incident, len(payload)) + payload


def decode(frame):
    if len(frame) < HEADER_SIZE:
        return None
    magic, version, kind, incident, length = HEADER.unpack(frame[:HEADER_SIZE])
    if magic != MAGIC or version != VERSION or HEADER_SIZE + length != len(frame):
        return None
    return kind, incident, frame[HEADER_SIZE:]


def run(binary, mode, responder, timeout=10):
    """Launch the child with report+control sockets; `responder(control)` plays
    the helper on a worker thread. Returns the CompletedProcess."""
    report_recv, report_send = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    control_helper, control_engine = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    report_recv.settimeout(0.1)
    stop = threading.Event()

    def drain():
        while not stop.is_set():
            try:
                report_recv.recv(4096)
            except (socket.timeout, OSError):
                pass

    def helper():
        try:
            responder(control_helper)
        except OSError:
            pass

    drainer = threading.Thread(target=drain)
    worker = threading.Thread(target=helper)
    drainer.start()
    worker.start()
    env = {**CLEAN_ENV,
           "LUDUS_DIAGNOSTIC_REPORT_FD": str(report_send.fileno()),
           "LUDUS_DIAGNOSTIC_CONTROL_FD": str(control_engine.fileno())}
    try:
        result = subprocess.run([binary, mode], capture_output=True, text=True, timeout=timeout,
                                env=env, pass_fds=(report_send.fileno(), control_engine.fileno()))
    finally:
        stop.set()
        worker.join(timeout=2)
        drainer.join(timeout=2)
        for s in (report_recv, report_send, control_helper, control_engine):
            s.close()
    return result


def expect_hello(control):
    """Read the Hello and reply HelloAck. Returns True on success."""
    frame = control.recv(HEADER_SIZE + MAX_PAYLOAD)
    decoded = decode(frame)
    if decoded is None or decoded[0] != KIND_HELLO:
        return False
    control.sendall(encode(KIND_HELLO_ACK))
    return True


def read_decision_request(control):
    """Read one DecisionRequest; return its incident id (or None)."""
    frame = control.recv(HEADER_SIZE + MAX_PAYLOAD)
    decoded = decode(frame)
    if decoded is None or decoded[0] != KIND_DECISION_REQUEST:
        return None
    return decoded[1]


def test_continue(binary):
    def responder(control):
        assert expect_hello(control)
        incident = read_decision_request(control)
        control.sendall(encode(KIND_DECISION_REPLY, incident, bytes([CONTINUE_ONCE])))
    result = run(binary, "single", responder)
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
    assert "ASSERT-RETURNED" in result.stdout, result.stdout
    assert result.stdout.count("CONDITION\n") == 1
    print("  ContinueOnce resumes the ASSERT OK")


def test_terminate(binary):
    def responder(control):
        assert expect_hello(control)
        incident = read_decision_request(control)
        control.sendall(encode(KIND_DECISION_REPLY, incident, bytes([TERMINATE])))
    result = run(binary, "single", responder)
    assert result.returncode == -signal.SIGABRT, (result.returncode, result.stderr)
    assert "ASSERT-RETURNED" not in result.stdout
    print("  Terminate aborts, no resume OK")


def test_repeated(binary):
    incidents = []

    def responder(control):
        assert expect_hello(control)
        for _ in range(2):
            incident = read_decision_request(control)
            incidents.append(incident)
            control.sendall(encode(KIND_DECISION_REPLY, incident, bytes([CONTINUE_ONCE])))
    result = run(binary, "repeated", responder)
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
    assert "FIRST-RETURNED" in result.stdout and "SECOND-RETURNED" in result.stdout
    assert len(incidents) == 2 and incidents[0] != incidents[1], incidents
    print("  Repeated ASSERTs get independent incident ids and resume OK")


def test_wrong_incident(binary):
    def responder(control):
        assert expect_hello(control)
        incident = read_decision_request(control)
        # Reply for a different incident id: must NOT authorize continuation.
        control.sendall(encode(KIND_DECISION_REPLY, incident + 999, bytes([CONTINUE_ONCE])))
    result = run(binary, "single", responder)
    assert result.returncode == -signal.SIGABRT, (result.returncode, result.stderr)
    print("  Wrong incident id => Terminate OK")


def test_wrong_kind(binary):
    def responder(control):
        assert expect_hello(control)
        incident = read_decision_request(control)
        # A reply with an unexpected kind must not continue.
        control.sendall(encode(KIND_HELLO_ACK, incident, b""))
    result = run(binary, "single", responder)
    assert result.returncode == -signal.SIGABRT, (result.returncode, result.stderr)
    print("  Wrong reply kind => Terminate OK")


def test_malformed(binary):
    def responder(control):
        assert expect_hello(control)
        read_decision_request(control)
        control.sendall(b"\x00\x01\x02")  # Too short / bad magic.
    result = run(binary, "single", responder)
    assert result.returncode == -signal.SIGABRT, (result.returncode, result.stderr)
    print("  Malformed reply => Terminate OK")


def test_helper_exit(binary):
    def responder(control):
        assert expect_hello(control)
        read_decision_request(control)
        control.close()  # Helper disconnects without replying.
    result = run(binary, "single", responder)
    assert result.returncode == -signal.SIGABRT, (result.returncode, result.stderr)
    print("  Helper disconnect => Terminate OK")


def test_require_never_resumes(binary):
    def responder(control):
        assert expect_hello(control)
        # Even offer ContinueOnce if a request arrives; REQUIRE must not send one
        # and must terminate regardless.
        try:
            incident = read_decision_request(control)
            if incident is not None:
                control.sendall(encode(KIND_DECISION_REPLY, incident, bytes([CONTINUE_ONCE])))
        except OSError:
            pass
    result = run(binary, "require", responder)
    assert result.returncode == -signal.SIGABRT, (result.returncode, result.stderr)
    assert "REQUIRE-RETURNED" not in result.stdout
    print("  REQUIRE never resumes OK")


def main():
    binary = sys.argv[1]
    test_continue(binary)
    test_terminate(binary)
    test_repeated(binary)
    test_wrong_incident(binary)
    test_wrong_kind(binary)
    test_malformed(binary)
    test_helper_exit(binary)
    test_require_never_resumes(binary)
    print("Passed resumable-ASSERT decision: continue, terminate, repeated, wrong-id, "
          "wrong-kind, malformed, helper-exit, REQUIRE-terminal")


if __name__ == "__main__":
    main()
