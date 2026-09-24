#!/usr/bin/env python3
"""Ludus external diagnostic helper (Linux, development tool).

A small standalone launcher/collector that runs OUTSIDE the engine process. It
owns the collector ends of two channels, launches the engine binary with the
engine-side descriptors, drains bounded report datagrams, answers the versioned
control handshake, and cleans up when the child exits.

It is a proposed development-tool dependency (Python standard library only for
this milestone); it is NOT a dependency of FoundationBase or any engine header.
The graphical Continue-once / Terminate dialog (Zenity) is a later milestone;
this milestone is report-only and never presents UI or decides ASSERT
continuation. The helper only acknowledges Hello and stands by.

The control wire format is the explicit little-endian byte encoding defined in
ludus/foundation/base/diagnostic_output.hpp (16-byte header + payload). Do NOT
send padded C++ structs.

Usage:
    ludus_diagnostic_helper.py [--report-only] [--] <engine-binary> [args...]
"""

import os
import selectors
import signal
import socket
import struct
import sys

# ControlHeader byte layout (little-endian):
#   uint32 magic, uint16 version, uint16 kind, uint32 incidentId, uint32 length
HEADER = struct.Struct("<IHHII")
MAGIC = 0x4C554443  # "LUDC"
VERSION = 1
HEADER_SIZE = HEADER.size  # 16
MAX_PAYLOAD = 2048

KIND_HELLO = 1
KIND_HELLO_ACK = 2
KIND_DECISION_REQUEST = 3
KIND_DECISION_REPLY = 4


def encode(kind, incident_id=0, payload=b""):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    return HEADER.pack(MAGIC, VERSION, kind, incident_id, len(payload)) + payload


def decode(frame):
    """Return (kind, incident_id, payload) or None for a malformed frame."""
    if len(frame) < HEADER_SIZE:
        return None
    magic, version, kind, incident_id, length = HEADER.unpack(frame[:HEADER_SIZE])
    if magic != MAGIC or version != VERSION:
        return None
    if length > MAX_PAYLOAD or HEADER_SIZE + length != len(frame):
        return None
    return kind, incident_id, frame[HEADER_SIZE:]


def answer_handshake(control):
    """Read one Hello and reply HelloAck. Returns True on success."""
    try:
        frame = control.recv(HEADER_SIZE + MAX_PAYLOAD)
    except OSError:
        return False
    decoded = decode(frame)
    if decoded is None or decoded[0] != KIND_HELLO:
        return False
    try:
        control.sendall(encode(KIND_HELLO_ACK))
    except OSError:
        return False
    return True


def main(argv):
    args = argv[1:]
    # Minimal option parse; --report-only accepted for forward-compat.
    while args and args[0].startswith("-"):
        if args[0] == "--report-only":
            args = args[1:]
        elif args[0] == "--":
            args = args[1:]
            break
        else:
            sys.stderr.write("[helper] unknown option\n")
            return 2
    if not args:
        sys.stderr.write("usage: ludus_diagnostic_helper.py [--report-only] [--] <engine-binary> [args...]\n")
        return 2

    # Report channel: connected AF_UNIX datagram pair (bounded report bytes).
    # Control channel: SOCK_SEQPACKET for framed handshake/decision messages.
    report_recv, report_send = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    control_helper, control_engine = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    # Do not let SIGPIPE from a dead engine kill the helper.
    signal.signal(signal.SIGPIPE, signal.SIG_IGN)

    engine_report_fd = report_send.fileno()
    engine_control_fd = control_engine.fileno()
    env = {
        **os.environ,
        "LUDUS_DIAGNOSTIC_REPORT_FD": str(engine_report_fd),
        "LUDUS_DIAGNOSTIC_CONTROL_FD": str(engine_control_fd),
    }

    pid = os.fork()
    if pid == 0:
        # Child: keep only the engine ends inheritable; exec the engine.
        os.set_inheritable(engine_report_fd, True)
        os.set_inheritable(engine_control_fd, True)
        try:
            os.execvpe(args[0], args, env)
        except OSError:
            os.write(2, b"[helper] failed to launch engine binary\n")
            os._exit(127)

    # Parent (collector). Close the engine ends we do not own.
    report_send.close()
    control_engine.close()

    # Handshake once, up front. Missing/malformed handshake is not fatal to the
    # helper; report delivery still works and the engine falls back to report-only.
    control_helper.setblocking(True)
    control_helper.settimeout(2.0)
    try:
        answer_handshake(control_helper)
    except socket.timeout:
        pass
    control_helper.settimeout(None)

    # Drain report datagrams to the helper's own stdout so a launcher/CI log
    # retains them independently of engine Logging. Keep draining while the
    # engine runs; never block on the child.
    report_recv.setblocking(False)
    sel = selectors.DefaultSelector()
    sel.register(report_recv, selectors.EVENT_READ)
    child_status = None
    while True:
        for key, _ in sel.select(timeout=0.1):
            del key
            try:
                while True:
                    data = report_recv.recv(4096)
                    if not data:
                        break
                    os.write(1, data)
            except BlockingIOError:
                pass
            except OSError:
                pass
        if child_status is None:
            waited, status = os.waitpid(pid, os.WNOHANG)
            if waited == pid:
                child_status = status
                continue  # One more drain pass for any queued datagrams.
        else:
            # Child gone; do a final non-blocking drain, then exit.
            try:
                while True:
                    data = report_recv.recv(4096)
                    if not data:
                        break
                    os.write(1, data)
            except (BlockingIOError, OSError):
                pass
            break

    sel.close()
    report_recv.close()
    control_helper.close()

    if os.WIFEXITED(child_status):
        return os.WEXITSTATUS(child_status)
    if os.WIFSIGNALED(child_status):
        return 128 + os.WTERMSIG(child_status)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
