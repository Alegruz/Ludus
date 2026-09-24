#!/usr/bin/env python3
"""Ludus external diagnostic helper (Linux, development tool).

A small standalone launcher/collector that runs OUTSIDE the engine process. It
owns the collector ends of two channels, launches the engine binary with the
engine-side descriptors, drains bounded report datagrams, answers the versioned
control handshake, and — for an eligible interactive ASSERT incident — presents a
Continue-once / Terminate decision and replies with the developer's choice. It
cleans up when the child exits.

It is a development-tool dependency (Python standard library only, plus Zenity
for the graphical dialog); it is NOT a dependency of FoundationBase or any engine
header. The failing engine thread never runs any of this UI code — presentation
is entirely out of process.

The control wire format is the explicit little-endian byte encoding defined in
ludus/foundation/base/diagnostic_output.hpp (16-byte header + payload). Do NOT
send padded C++ structs. Diagnostic report text is passed to Zenity via an
argument vector and rendered literally; it is never assembled into a shell
command.

Decision policy: only an explicit Continue-once answer returns ContinueOnce.
A cancelled/closed dialog, a missing UI tool, EOF on the prompt, or any error
resolves to Terminate. The helper never auto-continues.

Usage:
    ludus_diagnostic_helper.py [--report-only] [--] <engine-binary> [args...]
"""

import os
import selectors
import shutil
import signal
import socket
import struct
import subprocess
import sys
import threading

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

DECISION_TERMINATE = 0
DECISION_CONTINUE_ONCE = 1


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


def graphical_available():
    return bool(os.environ.get("WAYLAND_DISPLAY") or os.environ.get("DISPLAY")) and shutil.which("zenity")


def ask_decision(report_text):
    """Present Continue-once / Terminate and return DECISION_*.

    Graphical: Zenity question dialog (report shown literally via argv). Terminal:
    a tty/stdin y/N prompt. Anything other than an explicit continue — cancel,
    EOF, error, no channel — is Terminate. Never auto-continues.
    """
    prompt = (
        "A Ludus development ASSERT failed.\n\n"
        + report_text
        + "\n\nContinue once (resume past this assertion) or Terminate?"
    )
    if graphical_available():
        try:
            result = subprocess.run(
                ["zenity", "--question", "--no-markup", "--title=Ludus ASSERT",
                 "--ok-label=Continue once", "--cancel-label=Terminate", "--text", prompt],
                stdin=subprocess.DEVNULL,
            )
            # Zenity returns 0 for the OK/Continue button, non-zero otherwise.
            return DECISION_CONTINUE_ONCE if result.returncode == 0 else DECISION_TERMINATE
        except OSError:
            return DECISION_TERMINATE

    # Terminal fallback: read a single explicit answer from the controlling tty.
    try:
        tty_in = open("/dev/tty", "r", encoding="utf-8", errors="replace")
        tty_out = open("/dev/tty", "w", encoding="utf-8", errors="replace")
    except OSError:
        return DECISION_TERMINATE
    try:
        tty_out.write(prompt + "\n[c]ontinue once / [t]erminate (default terminate): ")
        tty_out.flush()
        answer = tty_in.readline()
    except OSError:
        return DECISION_TERMINATE
    finally:
        tty_in.close()
        tty_out.close()
    if not answer:  # EOF: never continue.
        return DECISION_TERMINATE
    return DECISION_CONTINUE_ONCE if answer.strip().lower() in ("c", "continue") else DECISION_TERMINATE


def main(argv):
    args = argv[1:]
    # Minimal option parse; --report-only forces Terminate for any decision.
    force_report_only = False
    while args and args[0].startswith("-"):
        if args[0] == "--report-only":
            force_report_only = True
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

    def drain_reports():
        try:
            while True:
                data = report_recv.recv(4096)
                if not data:
                    break
                os.write(1, data)
        except (BlockingIOError, OSError):
            pass

    report_recv.setblocking(False)
    control_helper.setblocking(False)
    sel = selectors.DefaultSelector()
    sel.register(report_recv, selectors.EVENT_READ, "report")
    sel.register(control_helper, selectors.EVENT_READ, "control")

    # A single in-flight decision, computed on a worker thread so report draining
    # continues while a dialog is open. Only one interactive incident at a time.
    pending = {"thread": None, "incident": None, "decision": None}

    def start_decision(incident_id, report_bytes):
        report_text = report_bytes.decode("utf-8", errors="replace")

        def worker():
            pending["decision"] = DECISION_TERMINATE if force_report_only else ask_decision(report_text)

        pending["incident"] = incident_id
        pending["thread"] = threading.Thread(target=worker, daemon=True)
        pending["thread"].start()

    def try_finish_decision():
        thread = pending["thread"]
        if thread is not None and not thread.is_alive():
            reply = encode(KIND_DECISION_REPLY, pending["incident"],
                           bytes([pending["decision"] if pending["decision"] is not None else DECISION_TERMINATE]))
            try:
                control_helper.sendall(reply)
            except OSError:
                pass
            pending["thread"] = None
            pending["incident"] = None
            pending["decision"] = None

    child_status = None
    while True:
        for key, _ in sel.select(timeout=0.1):
            if key.data == "report":
                drain_reports()
            elif key.data == "control":
                try:
                    frame = control_helper.recv(HEADER_SIZE + MAX_PAYLOAD)
                except (BlockingIOError, OSError):
                    frame = b""
                if not frame:
                    continue
                decoded = decode(frame)
                if decoded is None:
                    continue
                kind, incident_id, payload = decoded
                if kind == KIND_HELLO:
                    try:
                        control_helper.sendall(encode(KIND_HELLO_ACK))
                    except OSError:
                        pass
                elif kind == KIND_DECISION_REQUEST and pending["thread"] is None:
                    start_decision(incident_id, payload)

        try_finish_decision()

        if child_status is None:
            waited, status = os.waitpid(pid, os.WNOHANG)
            if waited == pid:
                child_status = status
                continue  # One more drain pass for any queued datagrams.
        else:
            drain_reports()
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
