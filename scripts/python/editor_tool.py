"""Ludus editor one-operation tooling adapter (configure / build / build_run).

This is a one-operation process, not a persistent daemon. The GUI (or a CLI test
harness) launches it with --stdio, sends exactly one request on stdin, keeps that
pipe open for cancellation / output credit / parent-lifetime detection, and reads
newline-delimited UTF-8 JSON events on stdout. stderr carries bounded adapter
diagnostics only. See .kiro/specs/editor-workspace/design.md sections 7-10.

Design guarantees implemented here:
  * Asynchronous selectors supervisor; no worker threads, no communicate() full
    buffers, no GUI-thread-equivalent blocking waits.
  * Each long command (configure/build/game) is spawned with Popen, shell=False,
    close_fds=True, stdin=DEVNULL, start_new_session=True, so the adapter owns a
    process group containing ordinary descendants.
  * Cancellation (explicit cancel message, stdin EOF, SIGTERM/SIGINT) latches and
    cleans up: TERM the group, drain, escalate to KILL after a deadline, observe
    termination, and only then emit the terminal result.
  * Non-reaping exit observation on Linux (os.waitid WNOWAIT) keeps the leader's
    PID valid until descendant cleanup is complete.
  * Bounded output credit window and bounded control output; no unbounded buffers.
  * Missing result / abnormal exit is never reported as success.

Bridge exit codes: 0 success, 1 failed, 2 protocol failure, 130 cancelled.
"""
from __future__ import annotations

import argparse
import errno
import json
import os
import selectors
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Optional

import editor_project


PROTOCOL_VERSION = 1

# Bounded resource limits (design section 10), conservative initial values.
OUTPUT_CREDIT_WINDOW = 256 * 1024       # encoded output bytes in flight
CONTROL_RESERVE_BYTES = 4 * 1024        # reserved for the terminal result frame
CONTROL_AGGREGATE_CAP = 1 * 1024 * 1024 # aggregate control output per job
CHILD_READ_CHUNK = 4 * 1024             # raw bytes per child read
MAX_PROTOCOL_LINE = 256 * 1024          # inbound control line cap (framing)
STDERR_CAP = 64 * 1024                  # adapter stderr per operation
READY_REQUEST_TIMEOUT = 5.0             # seconds to receive the one request
TERM_DEADLINE = 2.0                     # seconds before escalating TERM -> KILL
KILL_OBSERVE_DEADLINE = 1.0             # seconds to observe termination after KILL

# File API named client for the editor.
FILE_API_CLIENT = "ludus-editor"

# Supported native presets (design section 4/7).
PRESETS = ("linux-clang-debug", "linux-clang-development")


class ProtocolError(Exception):
    """Fatal protocol framing/version/type failure; cleans up then exits 2."""


# --------------------------------------------------------------------------- #
# Encoding helpers
# --------------------------------------------------------------------------- #
def hex16(value: int) -> str:
    """Encode a nonnegative integer as a 16-char lowercase hex string."""
    if value < 0 or value >= (1 << 64):
        raise ValueError("value out of range for 16-hex encoding")
    return f"{value:016x}"


def parse_hex16(text: Any) -> int:
    if not isinstance(text, str) or len(text) != 16:
        raise ProtocolError("expected a 16-character hex string")
    try:
        return int(text, 16)
    except ValueError as exc:
        raise ProtocolError("invalid hex string") from exc


class IncrementalDecoder:
    """UTF-8 decoder that replaces invalid sequences and never buffers unboundedly.

    Streaming child output may split multibyte characters across read chunks and
    may contain invalid UTF-8; this uses Python's incremental decoder with the
    'replace' error handler so a long line or invalid bytes cannot cause
    unbounded buffering (design section 10).
    """

    def __init__(self) -> None:
        import codecs

        self._decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")

    def decode(self, data: bytes, final: bool = False) -> str:
        return self._decoder.decode(data, final)


# --------------------------------------------------------------------------- #
# Protocol writer with bounded output-credit and control accounting
# --------------------------------------------------------------------------- #
class ProtocolWriter:
    """Writes JSON-line events to stdout honoring the output-credit window.

    Output frames consume credit (their complete encoded byte count including the
    newline). Control events (phase/command/targets/runtime_started/result) do
    not consume credit but their aggregate encoded size is capped, reserving
    space for the terminal result. When output credit is exhausted, ordinary
    output is dropped and counted rather than buffered.
    """

    def __init__(self, job: str, write: Callable[[bytes], None]) -> None:
        self._job = job
        self._write = write
        self._credit = OUTPUT_CREDIT_WINDOW
        self._output_total = 0        # cumulative encoded output bytes emitted
        self._acknowledged = 0        # last acknowledged boundary
        self._control_total = 0       # aggregate control bytes emitted
        self.dropped_blocks = 0
        self.dropped_bytes = 0

    @property
    def output_total(self) -> int:
        return self._output_total

    def grant_credit(self, through: int) -> None:
        """Apply an acknowledgement up to `through` cumulative output bytes."""
        if through > self._output_total:
            raise ProtocolError("credit acknowledges a future/non-boundary offset")
        if through < self._acknowledged:
            return  # stale/duplicate acknowledgements are idempotent
        self._acknowledged = through
        # Credit equals the fixed window minus outstanding (unacknowledged) bytes.
        outstanding = self._output_total - self._acknowledged
        self._credit = OUTPUT_CREDIT_WINDOW - outstanding

    def _emit(self, obj: dict) -> bytes:
        obj["protocol"] = PROTOCOL_VERSION
        line = (json.dumps(obj, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
        return line

    def control(self, obj: dict, *, reserved: bool = False) -> None:
        obj.setdefault("job", self._job)
        line = self._emit(obj)
        budget = CONTROL_AGGREGATE_CAP if reserved else (CONTROL_AGGREGATE_CAP - CONTROL_RESERVE_BYTES)
        if self._control_total + len(line) > budget:
            raise ProtocolError("aggregate control output exceeded its bound")
        self._control_total += len(line)
        self._write(line)

    def output(self, stage: str, stream: str, text: str) -> None:
        if not text:
            return
        obj = {"job": self._job, "type": "output", "stage": stage, "stream": stream, "text": text}
        line = self._emit(obj)
        # Determine the encoded size up front so end_offset reflects it.
        size = len(line)
        if size > self._credit:
            # No credit: drop and count rather than buffer (keep draining child).
            self.dropped_blocks += 1
            self.dropped_bytes += len(text.encode("utf-8"))
            return
        self._output_total += size
        obj["end_offset"] = hex16(self._output_total)
        self._credit -= size
        self._write(self._emit(obj))

    def ready(self) -> None:
        # ready has protocol/type only; it does not echo the job.
        self._write(self._emit({"type": "ready"}))

    def phase(self, stage: str) -> None:
        self.control({"type": "phase", "stage": stage})

    def command(self, stage: str, argv: list[str], cwd: str) -> None:
        self.control({"type": "command", "stage": stage, "argv": list(argv), "cwd": cwd})

    def targets(self, names: list[str], preset: str) -> None:
        self.control({"type": "targets", "targets": list(names), "preset": preset})

    def runtime_started(self, pid: int, executable: str, cwd: str, args: list[str]) -> None:
        self.control({"type": "runtime_started", "pid": pid, "executable": executable,
                      "cwd": cwd, "args": list(args)})

    def result(self, outcome: str, stage: str, code: str, message: str,
               cleanup_confirmed: bool, exit_code: Optional[int], sig: Optional[int]) -> None:
        message = message.encode("utf-8")[:256].decode("utf-8", "ignore")
        self.control({"type": "result", "outcome": outcome, "stage": stage, "code": code,
                      "message": message, "cleanup_confirmed": cleanup_confirmed,
                      "exit_code": exit_code, "signal": sig}, reserved=True)


# --------------------------------------------------------------------------- #
# Owned child process group and non-reaping cleanup (Linux)
# --------------------------------------------------------------------------- #
@dataclass
class CleanupReport:
    confirmed: bool
    forced: bool = False
    exit_code: Optional[int] = None
    signal: Optional[int] = None


class OwnedProcess:
    """One Popen child in its own session/process group, with group cleanup.

    The child is started with start_new_session=True so it leads a new process
    group that includes its ordinary descendants. Cleanup signals the whole group
    and uses non-reaping exit observation so the leader's PID stays valid (not
    reusable as a new group) until descendant cleanup completes.
    """

    def __init__(self, argv: list[str], cwd: str, env: dict[str, str]) -> None:
        self.argv = list(argv)
        self.cwd = cwd
        self._proc = subprocess.Popen(
            self.argv,
            cwd=cwd,
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            close_fds=True,
            start_new_session=True,
        )
        self.pid = self._proc.pid

    @property
    def stdout_fd(self) -> int:
        return self._proc.stdout.fileno()

    @property
    def stderr_fd(self) -> int:
        return self._proc.stderr.fileno()

    def read_stdout(self, size: int) -> bytes:
        return os.read(self.stdout_fd, size)

    def read_stderr(self, size: int) -> bytes:
        return os.read(self.stderr_fd, size)

    def _observe_exit_nowait(self) -> Optional[tuple[Optional[int], Optional[int]]]:
        """Observe the leader's exit WITHOUT reaping it (WNOWAIT).

        Returns (exit_code, signal) if the leader has exited, else None. Using
        waitid(WNOWAIT) keeps the zombie leader present so its PID cannot be
        reused as a new, unrelated process group during cleanup.
        """
        try:
            info = os.waitid(os.P_PID, self.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
        except ChildProcessError:
            return (None, None)
        except OSError as exc:
            if exc.errno == errno.ECHILD:
                return (None, None)
            raise
        if info is None:
            return None
        if info.si_code == os.CLD_EXITED:
            return (info.si_status, None)
        # Killed/dumped by a signal.
        return (None, info.si_status)

    def _group_members(self) -> list[int]:
        """Live members of the owned group (excluding the zombie leader).

        Reads /proc to find processes whose process-group id equals the leader's
        pid. Inability to inspect is NOT treated as proof of absence; the caller
        distinguishes that from confirmed emptiness.
        """
        members: list[int] = []
        proc_root = Path("/proc")
        try:
            entries = [p for p in proc_root.iterdir() if p.name.isdigit()]
        except OSError:
            return members
        for entry in entries:
            pid = int(entry.name)
            if pid == self.pid:
                continue  # exclude the zombie leader itself
            try:
                stat = (entry / "stat").read_text()
            except OSError:
                continue
            # pgid is the 5th field after the (comm) which may contain spaces;
            # split on the last ')' to skip the comm field safely.
            try:
                after = stat.rsplit(")", 1)[1].split()
                pgid = int(after[2])  # fields: state ppid pgrp ...
            except (IndexError, ValueError):
                continue
            if pgid == self.pid:
                members.append(pid)
        return members

    def _signal_group(self, sig: int) -> None:
        try:
            os.killpg(self.pid, sig)
        except ProcessLookupError:
            pass
        except PermissionError:
            pass

    def _close_pipes(self) -> None:
        for stream in (self._proc.stdout, self._proc.stderr):
            try:
                if stream is not None and not stream.closed:
                    stream.close()
            except OSError:
                pass

    def _reap_leader(self) -> tuple[Optional[int], Optional[int]]:
        self._close_pipes()
        try:
            _, status = os.waitpid(self.pid, 0)
        except ChildProcessError:
            if self._proc.returncode is None:
                self._proc.returncode = 0
            return (self._proc.returncode, None)
        # Record the outcome on the Popen so its finalizer does not warn or try to
        # reap an already-reaped pid.
        if os.WIFEXITED(status):
            self._proc.returncode = os.WEXITSTATUS(status)
            return (os.WEXITSTATUS(status), None)
        if os.WIFSIGNALED(status):
            self._proc.returncode = -os.WTERMSIG(status)
            return (None, os.WTERMSIG(status))
        self._proc.returncode = 0
        return (None, None)

    def cleanup(self, *, cancelled: bool, now: Callable[[], float] = time.monotonic,
                sleep: Callable[[float], None] = lambda s: time.sleep(min(s, 0.02))) -> CleanupReport:
        """Terminate the owned group and confirm cleanup within deadlines.

        Returns a CleanupReport. confirmed=False (CleanupUnknown) when a member
        could not be observed to terminate within the deadlines; this is never
        reported as success.
        """
        forced = False
        # Signal the whole group with TERM, then wait for the leader to exit.
        self._signal_group(signal.SIGTERM)
        deadline = now() + TERM_DEADLINE
        leader_exit: Optional[tuple[Optional[int], Optional[int]]] = None
        while now() < deadline:
            observed = self._observe_exit_nowait()
            if observed is not None and (observed[0] is not None or observed[1] is not None):
                leader_exit = observed
                break
            sleep(0.01)
        if leader_exit is None or (leader_exit[0] is None and leader_exit[1] is None):
            # Escalate to KILL and observe for a further bounded window.
            forced = True
            self._signal_group(signal.SIGKILL)
            kill_deadline = now() + KILL_OBSERVE_DEADLINE
            while now() < kill_deadline:
                observed = self._observe_exit_nowait()
                if observed is not None and (observed[0] is not None or observed[1] is not None):
                    leader_exit = observed
                    break
                sleep(0.01)

        # Clean up any surviving descendants with the same escalation, keeping
        # the leader waitable (not reaped) so its pid is not reused as a group.
        members = self._group_members()
        if members:
            self._signal_group(signal.SIGKILL)
            member_deadline = now() + KILL_OBSERVE_DEADLINE
            while now() < member_deadline and self._group_members():
                sleep(0.01)
            members = self._group_members()

        # Now reap the leader (the zombie) to release its pid.
        exit_code, sig = (None, None)
        if leader_exit is not None:
            exit_code, sig = leader_exit
        reaped = self._reap_leader()
        if exit_code is None and sig is None:
            exit_code, sig = reaped

        confirmed = not members
        return CleanupReport(confirmed=confirmed, forced=forced, exit_code=exit_code, signal=sig)

    def wait_exit_nowait(self) -> Optional[tuple[Optional[int], Optional[int]]]:
        """Public non-reaping exit check for the normal (non-cancel) path."""
        return self._observe_exit_nowait()

    def finalize_normal(self) -> CleanupReport:
        """Normal direct-child exit still requires descendant cleanup.

        The leader has exited; observe it without reaping, verify no descendants
        remain in the owned group, then reap the leader.
        """
        observed = self._observe_exit_nowait() or (None, None)
        members = self._group_members()
        if members:
            # Ordinary descendants outlived the leader (e.g. a surviving writer):
            # terminate them with deadlines before reaping the leader.
            self._signal_group(signal.SIGTERM)
            deadline = time.monotonic() + TERM_DEADLINE
            while time.monotonic() < deadline and self._group_members():
                time.sleep(0.01)
            if self._group_members():
                self._signal_group(signal.SIGKILL)
                kdeadline = time.monotonic() + KILL_OBSERVE_DEADLINE
                while time.monotonic() < kdeadline and self._group_members():
                    time.sleep(0.01)
            members = self._group_members()
        exit_code, sig = observed
        reaped = self._reap_leader()
        if exit_code is None and sig is None:
            exit_code, sig = reaped
        return CleanupReport(confirmed=not members, forced=False, exit_code=exit_code, signal=sig)


# --------------------------------------------------------------------------- #
# Command planning for the two providers
# --------------------------------------------------------------------------- #
@dataclass
class ToolContext:
    tooling_root: Path
    engine: Any
    cmake_targets: Any


@dataclass
class Plan:
    """Resolved, immutable plan for one operation built from a saved descriptor."""
    descriptor: editor_project.Descriptor
    source_dir: Path
    build_dir: Path
    cmake: list[str]            # absolute managed cmake argv prefix
    env: dict[str, str]
    run_cwd: Path


def _resolve_managed_cmake(context: ToolContext) -> str:
    cmake_path = context.engine.cmake(context.tooling_root)
    if not Path(cmake_path).exists():
        raise editor_project.ProjectError(
            "MissingTools",
            "the managed CMake is not available; run ./init.sh in the tooling checkout first",
        )
    return str(cmake_path)


def build_plan(context: ToolContext, descriptor: editor_project.Descriptor, project_dir: Path) -> Plan:
    """Resolve directories, tools and environment for the descriptor's provider.

    Paths are resolved relative to the descriptor directory (source_dir) and the
    resolved source (run.cwd), never relative to the adapter's launch directory.
    """
    engine = context.engine
    source_dir = (project_dir / descriptor.source_dir).resolve()
    if not source_dir.is_dir():
        raise editor_project.ProjectError("InvalidProject", f"source directory does not exist: {source_dir}")
    if len(str(source_dir).encode("utf-8")) > 4096:
        raise editor_project.ProjectError("InvalidProject", "resolved source path exceeds 4096 bytes")
    if not (source_dir / "CMakeLists.txt").is_file() or not (source_dir / "CMakePresets.json").is_file():
        raise editor_project.ProjectError(
            "InvalidProject", "source must contain CMakeLists.txt and CMakePresets.json")

    run_cwd = (source_dir / descriptor.run_cwd).resolve()

    cmake = [_resolve_managed_cmake(context)]

    if descriptor.provider == "ludus":
        # Verify the opened source is a Ludus checkout and reuse its bootstrap,
        # managed tools and prepared environment. Missing/stale preparation is an
        # actionable error that directs the user to run init explicitly.
        if not (source_dir / "config" / "tool_versions.json").is_file():
            raise editor_project.ProjectError(
                "InvalidProject", "provider 'ludus' requires a Ludus engine checkout as source")
        try:
            engine.ensure_bootstrap_for_preset(source_dir, descriptor.preset)
        except engine.EngineError as exc:
            raise editor_project.ProjectError("BootstrapStale", str(exc)) from exc
        build_dir = engine.build_dir_for_preset(source_dir, descriptor.preset)
        env = engine.tool_env(source_dir)
    else:
        # External project: use the trusted tooling checkout's managed tools with
        # the external source's own CMakePresets.json. The two supported native
        # presets must produce single-config Ninja trees under out/build/<preset>.
        build_dir = source_dir / "out" / "build" / descriptor.preset
        env = engine.tool_env(context.tooling_root)
        # The external sample supplies CMAKE_PREFIX_PATH via $env{LUDUS_SDK_PREFIX}
        # in its preset; the adapter does not choose/build/install an SDK.

    return Plan(
        descriptor=descriptor,
        source_dir=source_dir,
        build_dir=build_dir,
        cmake=cmake,
        env=env,
        run_cwd=run_cwd,
    )


def configure_argv(plan: Plan) -> list[str]:
    return [*plan.cmake, "--preset", plan.descriptor.preset, "-B", str(plan.build_dir)]


def build_argv(plan: Plan) -> list[str]:
    return [*plan.cmake, "--build", str(plan.build_dir), "--target", plan.descriptor.target]


# --------------------------------------------------------------------------- #
# Supervisor: runs one owned command to completion while servicing control I/O
# --------------------------------------------------------------------------- #
class Cancelled(Exception):
    """Raised internally when cancellation is latched; drives orderly cleanup.

    When raised after a child group has been cleaned up, `report` carries that
    CleanupReport so the caller can report cleanup confirmation accurately.
    """

    def __init__(self, report: Optional["CleanupReport"] = None) -> None:
        super().__init__("cancelled")
        self.report = report


class Supervisor:
    """Event loop around one OwnedProcess plus the control (stdin) pipe.

    Control messages (cancel/credit) and parent EOF are serviced while the child
    runs. No worker threads; a single selectors loop multiplexes the control fd
    and both child pipes with monotonic deadlines.
    """

    def __init__(self, job: str, writer: ProtocolWriter, control_fd: int,
                 on_control: Callable[[dict], None]) -> None:
        self._job = job
        self._writer = writer
        self._control_fd = control_fd
        self._on_control = on_control
        self._control_buf = b""
        self.cancel_latched = False
        self._stderr_decoders: dict[int, IncrementalDecoder] = {}

    def latch_cancel(self) -> None:
        self.cancel_latched = True

    def _read_control(self) -> None:
        try:
            chunk = os.read(self._control_fd, CHILD_READ_CHUNK)
        except (BlockingIOError, InterruptedError):
            return
        except OSError:
            chunk = b""
        if not chunk:
            # stdin EOF is parent loss and cancels the operation.
            self.latch_cancel()
            raise Cancelled()
        self._control_buf += chunk
        if len(self._control_buf) > MAX_PROTOCOL_LINE:
            raise ProtocolError("inbound control line exceeded its bound")
        while b"\n" in self._control_buf:
            line, self._control_buf = self._control_buf.split(b"\n", 1)
            if not line.strip():
                continue
            try:
                message = json.loads(line.decode("utf-8"))
            except (ValueError, UnicodeDecodeError) as exc:
                raise ProtocolError("malformed control frame") from exc
            if not isinstance(message, dict) or message.get("protocol") != PROTOCOL_VERSION:
                raise ProtocolError("bad control protocol/version")
            mtype = message.get("type")
            if mtype == "cancel":
                if message.get("job") != self._job:
                    raise ProtocolError("control job mismatch")
                self.latch_cancel()
                raise Cancelled()
            if mtype == "credit":
                if message.get("job") != self._job:
                    raise ProtocolError("control job mismatch")
                self._writer.grant_credit(parse_hex16(message.get("through")))
                continue
            raise ProtocolError(f"unknown control type {mtype!r}")

    def run_command(self, stage: str, child: OwnedProcess) -> CleanupReport:
        """Drain the child's streams while servicing control, until it exits.

        Returns the finalized CleanupReport. On cancellation or a protocol error
        the owned child group is cleaned up with deadlines before the exception
        propagates, so a long-running configure/build child is never leaked.
        """
        try:
            return self._run_command_loop(stage, child)
        except Cancelled:
            # Clean up the owned group with deadlines, then re-raise carrying the
            # cleanup report so the caller can report confirmation accurately.
            report = child.cleanup(cancelled=True)
            raise Cancelled(report)
        except ProtocolError:
            child.cleanup(cancelled=True)
            raise

    def _run_command_loop(self, stage: str, child: OwnedProcess) -> CleanupReport:
        sel = selectors.DefaultSelector()
        os.set_blocking(self._control_fd, False)
        os.set_blocking(child.stdout_fd, False)
        os.set_blocking(child.stderr_fd, False)
        sel.register(self._control_fd, selectors.EVENT_READ, "control")
        sel.register(child.stdout_fd, selectors.EVENT_READ, "stdout")
        sel.register(child.stderr_fd, selectors.EVENT_READ, "stderr")
        out_decoder = IncrementalDecoder()
        err_decoder = IncrementalDecoder()
        stderr_bytes = 0
        open_streams = {"stdout", "stderr"}
        try:
            while True:
                for key, _ in sel.select(timeout=0.1):
                    tag = key.data
                    if tag == "control":
                        self._read_control()
                    elif tag == "stdout":
                        data = child.read_stdout(CHILD_READ_CHUNK)
                        if not data:
                            open_streams.discard("stdout")
                            try:
                                sel.unregister(child.stdout_fd)
                            except KeyError:
                                pass
                        else:
                            self._writer.output(stage, "stdout", out_decoder.decode(data))
                    elif tag == "stderr":
                        data = child.read_stderr(CHILD_READ_CHUNK)
                        if not data:
                            open_streams.discard("stderr")
                            try:
                                sel.unregister(child.stderr_fd)
                            except KeyError:
                                pass
                        else:
                            stderr_bytes += len(data)
                            self._writer.output(stage, "stderr", err_decoder.decode(data))
                # The command is done when both pipes are at EOF and the leader
                # has exited; observe without reaping first.
                if not open_streams:
                    observed = child.wait_exit_nowait()
                    if observed is not None:
                        break
        finally:
            sel.close()
        return child.finalize_normal()


# --------------------------------------------------------------------------- #
# Operation driver
# --------------------------------------------------------------------------- #
@dataclass
class OperationResult:
    outcome: str          # success/failed/cancelled
    stage: str
    code: str
    message: str
    cleanup_confirmed: bool = True
    exit_code: Optional[int] = None
    signal: Optional[int] = None


class Operation:
    """Runs one configure/build/build_run operation under a Supervisor."""

    def __init__(self, context: ToolContext, writer: ProtocolWriter, job: str,
                 operation: str, project_path: Path, expected_sha256: str,
                 control_fd: int) -> None:
        self._context = context
        self._writer = writer
        self._job = job
        self._operation = operation
        self._project_path = project_path
        self._expected = expected_sha256
        self._supervisor = Supervisor(job, writer, control_fd, lambda _m: None)
        self._stage = "configuring"

    def latch_cancel(self) -> None:
        self._supervisor.latch_cancel()

    def _check_cancel(self) -> None:
        if self._supervisor.cancel_latched:
            raise Cancelled()

    def _load_clean_descriptor(self) -> editor_project.Descriptor:
        import hashlib

        try:
            data = self._project_path.read_bytes()
        except OSError as exc:
            raise editor_project.ProjectError("InvalidProject", f"cannot read descriptor: {exc}") from exc
        digest = hashlib.sha256(data).hexdigest()
        if self._expected and digest != self._expected:
            raise editor_project.ProjectError(
                "Conflict", "descriptor digest does not match the expected clean snapshot")
        # Revalidate all fields before executing any tool.
        return editor_project.parse_descriptor_bytes(data)

    def _run_stage(self, stage: str, argv: list[str], cwd: Path, env: dict[str, str],
                   code_on_fail: str) -> None:
        self._stage = stage
        self._writer.phase(stage)
        self._writer.command(stage, argv, str(cwd))
        self._check_cancel()
        child = OwnedProcess(argv, str(cwd), env)
        report = self._supervisor.run_command(stage, child)
        if not report.confirmed:
            raise _CleanupUnknown(stage, report)
        if report.exit_code not in (0, None) or report.signal is not None:
            raise _StageFailed(stage, code_on_fail, report)

    def execute(self) -> OperationResult:
        try:
            descriptor = self._load_clean_descriptor()
            plan = build_plan(self._context, descriptor, self._project_path.parent)

            # Named codemodel query before configure; the reply is read after a
            # successful configure.
            self._context.cmake_targets.query_codemodel(plan.build_dir, FILE_API_CLIENT)
            self._check_cancel()
            self._run_stage("configuring", configure_argv(plan), plan.source_dir, plan.env, "ConfigureFailed")

            # Discover executable targets; verify source/build identity.
            self._context.cmake_targets.verify_identity(plan.build_dir, plan.source_dir,
                                                        self._context.engine, client=FILE_API_CLIENT)
            names = self._context.cmake_targets.list_executable_targets(
                plan.build_dir, self._context.engine, client=FILE_API_CLIENT)
            self._writer.targets(names, descriptor.preset)

            if self._operation == "configure":
                return OperationResult("success", "configuring", "Ok", "configure complete")

            # Resolve the selected executable target before building it.
            self._resolve_executable(plan)
            self._check_cancel()
            self._run_stage("building", build_argv(plan), plan.source_dir, plan.env, "BuildFailed")

            # CMake may regenerate during build: reread the current codemodel and
            # re-resolve the artifact before Run.
            self._context.cmake_targets.verify_identity(plan.build_dir, plan.source_dir,
                                                        self._context.engine, client=FILE_API_CLIENT)
            artifact = self._resolve_executable(plan)
            if not (artifact.is_file() and os.access(artifact, os.X_OK)):
                raise _StageFailed("building", "ArtifactInvalid", None,
                                   message=f"built artifact is missing or not executable: {artifact}")

            if self._operation == "build":
                return OperationResult("success", "building", "Ok", "build complete")

            # Cancellation wins if accepted before runtime spawn.
            self._check_cancel()
            return self._launch_runtime(plan, artifact)
        except Cancelled as exc:
            confirmed = exc.report.confirmed if exc.report is not None else True
            code = "Cancelled" if confirmed else "CleanupUnknown"
            outcome = "cancelled" if confirmed else "failed"
            exit_code = exc.report.exit_code if exc.report else None
            sig = exc.report.signal if exc.report else None
            return OperationResult(outcome, self._stage, code, "operation cancelled",
                                   cleanup_confirmed=confirmed, exit_code=exit_code, signal=sig)
        except _CleanupUnknown as exc:
            return OperationResult("failed", exc.stage, "CleanupUnknown", "cleanup could not be confirmed",
                                   cleanup_confirmed=False,
                                   exit_code=exc.report.exit_code, signal=exc.report.signal)
        except _StageFailed as exc:
            return OperationResult("failed", exc.stage, exc.code, exc.message,
                                   exit_code=exc.exit_code, signal=exc.signal)
        except editor_project.ProjectError as exc:
            return OperationResult("failed", self._stage, exc.code, exc.message)
        except self._context.engine.EngineError as exc:
            # File API read/validation or bootstrap failure after configure:
            # a reply/identity problem, not a crash.
            return OperationResult("failed", self._stage, "ReplyInvalid", str(exc))

    def _resolve_executable(self, plan: Plan) -> Path:
        try:
            return self._context.cmake_targets.target_executable(
                plan.build_dir, plan.descriptor.target, self._context.engine, client=FILE_API_CLIENT)
        except self._context.engine.EngineError as exc:
            raise _StageFailed(self._stage, "TargetInvalid", None, message=str(exc)) from exc

    def _launch_runtime(self, plan: Plan, artifact: Path) -> OperationResult:
        self._stage = "launching"
        self._writer.phase("launching")
        run_argv = [str(artifact), *plan.descriptor.run_args]
        self._writer.command("launching", run_argv, str(plan.run_cwd))
        if not plan.run_cwd.is_dir():
            raise _StageFailed("launching", "SpawnFailed", None,
                               message=f"run working directory does not exist: {plan.run_cwd}")
        self._check_cancel()
        try:
            child = OwnedProcess(run_argv, str(plan.run_cwd), plan.env)
        except OSError as exc:
            raise _StageFailed("launching", "SpawnFailed", None, message=str(exc)) from exc
        self._stage = "running"
        self._writer.phase("running")
        self._writer.runtime_started(child.pid, str(artifact), str(plan.run_cwd), plan.descriptor.run_args)
        try:
            report = self._supervisor.run_command("running", child)
        except Cancelled as exc:
            # run_command already cleaned up the owned group with deadlines.
            report = exc.report if exc.report is not None else child.cleanup(cancelled=True)
            self._stage = "stopping"
            self._writer.phase("stopping")
            if not report.confirmed:
                return OperationResult("failed", "stopping", "CleanupUnknown",
                                       "cleanup could not be confirmed", cleanup_confirmed=False,
                                       exit_code=report.exit_code, signal=report.signal)
            return OperationResult("cancelled", "stopping", "Cancelled", "runtime stopped",
                                   cleanup_confirmed=True, exit_code=report.exit_code, signal=report.signal)
        if not report.confirmed:
            return OperationResult("failed", "running", "CleanupUnknown",
                                   "cleanup could not be confirmed", cleanup_confirmed=False,
                                   exit_code=report.exit_code, signal=report.signal)
        if report.signal is not None:
            return OperationResult("failed", "running", "RuntimeSignaled",
                                   f"runtime terminated by signal {report.signal}",
                                   exit_code=None, signal=report.signal)
        if report.exit_code not in (0, None):
            return OperationResult("failed", "running", "RuntimeFailed",
                                   f"runtime exited with code {report.exit_code}",
                                   exit_code=report.exit_code)
        return OperationResult("success", "running", "Ok", "runtime exited cleanly",
                               exit_code=report.exit_code)


class _StageFailed(Exception):
    def __init__(self, stage: str, code: str, report: Optional[CleanupReport], message: str = "") -> None:
        super().__init__(message or code)
        self.stage = stage
        self.code = code
        self.message = message or f"{stage} failed"
        self.exit_code = report.exit_code if report else None
        self.signal = report.signal if report else None


class _CleanupUnknown(Exception):
    def __init__(self, stage: str, report: CleanupReport) -> None:
        super().__init__("cleanup unknown")
        self.stage = stage
        self.report = report


# --------------------------------------------------------------------------- #
# stdio handshake and main
# --------------------------------------------------------------------------- #
def _read_one_request(control_fd: int, deadline: float) -> dict:
    """Read exactly one request line within the deadline. Raises on timeout/EOF."""
    buf = b""
    os.set_blocking(control_fd, False)
    sel = selectors.DefaultSelector()
    sel.register(control_fd, selectors.EVENT_READ)
    try:
        while time.monotonic() < deadline:
            for _ in sel.select(timeout=0.1):
                try:
                    chunk = os.read(control_fd, CHILD_READ_CHUNK)
                except (BlockingIOError, InterruptedError):
                    continue
                if not chunk:
                    raise ProtocolError("stdin closed before a request was received")
                buf += chunk
                if len(buf) > MAX_PROTOCOL_LINE:
                    raise ProtocolError("request line exceeded its bound")
                if b"\n" in buf:
                    line, _ = buf.split(b"\n", 1)
                    try:
                        message = json.loads(line.decode("utf-8"))
                    except (ValueError, UnicodeDecodeError) as exc:
                        raise ProtocolError("malformed request frame") from exc
                    return message
        raise ProtocolError("no request received within 5 seconds")
    finally:
        sel.close()


def _validate_request(message: dict) -> tuple[str, str, Path, str]:
    if not isinstance(message, dict) or message.get("protocol") != PROTOCOL_VERSION:
        raise ProtocolError("bad request protocol/version")
    job = message.get("job")
    if not isinstance(job, str) or len(job) != 16:
        raise ProtocolError("request job must be a 16-character hex string")
    try:
        int(job, 16)
    except ValueError as exc:
        raise ProtocolError("request job is not hex") from exc
    operation = message.get("operation")
    if operation not in ("configure", "build", "build_run"):
        raise ProtocolError("unknown operation")
    project = message.get("project")
    if not isinstance(project, str) or not os.path.isabs(project):
        raise ProtocolError("request project must be an absolute path")
    expected = message.get("expected_sha256", "")
    if not isinstance(expected, str) or (expected and len(expected) != 64):
        raise ProtocolError("expected_sha256 must be 64 hex characters")
    return job, operation, Path(project), expected


def run_stdio(context: ToolContext, in_fd: int, out_fd: int) -> int:
    """Run the single-operation stdio protocol. Returns the bridge exit code."""
    def write(data: bytes) -> None:
        os.write(out_fd, data)

    # Minimal pre-handshake writer (no job yet) just for the `ready` frame.
    def write_ready() -> None:
        write((json.dumps({"protocol": PROTOCOL_VERSION, "type": "ready"}) + "\n").encode("utf-8"))

    try:
        write_ready()
        message = _read_one_request(in_fd, time.monotonic() + READY_REQUEST_TIMEOUT)
        job, operation, project, expected = _validate_request(message)
    except ProtocolError as exc:
        try:
            write((json.dumps({"protocol": PROTOCOL_VERSION, "type": "result", "outcome": "failed",
                               "stage": "configuring", "code": "ProtocolError", "message": str(exc)[:256],
                               "cleanup_confirmed": True, "exit_code": None, "signal": None}) + "\n")
                  .encode("utf-8"))
        except OSError:
            pass
        return 2

    writer = ProtocolWriter(job, write)
    operation_obj = Operation(context, writer, job, operation, project, expected, in_fd)

    # SIGTERM/SIGINT only latch cancellation; cleanup runs before exit.
    def _handle_signal(_signum, _frame):
        operation_obj.latch_cancel()

    try:
        signal.signal(signal.SIGTERM, _handle_signal)
        signal.signal(signal.SIGINT, _handle_signal)
    except (ValueError, OSError):
        pass  # not on the main thread (e.g. under test); the loop still checks EOF

    try:
        result = operation_obj.execute()
    except ProtocolError as exc:
        writer.result("failed", operation_obj._stage, "ProtocolError", str(exc), True, None, None)
        return 2

    writer.result(result.outcome, result.stage, result.code, result.message,
                  result.cleanup_confirmed, result.exit_code, result.signal)

    if not result.cleanup_confirmed:
        return 1
    if result.outcome == "cancelled":
        return 130
    if result.outcome == "success":
        return 0
    return 1


def _make_context(tooling_root: Path) -> ToolContext:
    import engine as engine_module
    import cmake_targets as cmake_targets_module

    return ToolContext(tooling_root=tooling_root, engine=engine_module, cmake_targets=cmake_targets_module)


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Ludus editor tooling adapter")
    parser.add_argument("--stdio", action="store_true", required=True,
                        help="run the one-operation stdio protocol")
    parser.add_argument("--tooling-root", required=True, help="absolute trusted Ludus tooling checkout")
    args = parser.parse_args(argv)

    tooling_root = Path(args.tooling_root).resolve()
    context = _make_context(tooling_root)
    return run_stdio(context, sys.stdin.fileno(), sys.stdout.fileno())


if __name__ == "__main__":
    sys.exit(main())
