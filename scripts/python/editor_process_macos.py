"""Darwin-only non-reaping child observation and bounded process-group inventory.

Python exposes waitid on Linux, but Darwin's libc API is needed on macOS.
The owner must retain the zombie leader until group cleanup is confirmed.
"""
from __future__ import annotations

import ctypes
import errno

# Thanks to Apple, XNU BSD API headers sys/wait.h (waitid/WNOWAIT),
# sys/signal.h (siginfo_t), and sys/proc_info.h (process inventory): the SDK
# declarations define these 64-bit Darwin FFI layouts. This binds the system
# APIs rather than copying implementation code or reaping early.
# https://github.com/apple-oss-distributions/xnu/tree/main/bsd/sys
# Also checked against the prepared macOS SDK's libproc.h declarations.


class _SignalInfo(ctypes.Structure):
    _fields_ = [("signo", ctypes.c_int), ("error", ctypes.c_int),
                ("code", ctypes.c_int), ("pid", ctypes.c_int),
                ("uid", ctypes.c_uint), ("status", ctypes.c_int),
                ("address", ctypes.c_void_p), ("value", ctypes.c_void_p),
                ("band", ctypes.c_long), ("reserved", ctypes.c_ulong * 7)]


class _ShortInfo(ctypes.Structure):
    _fields_ = [("pid", ctypes.c_uint), ("parent", ctypes.c_uint),
                ("group", ctypes.c_uint), ("status", ctypes.c_uint),
                ("name", ctypes.c_char * 16), ("reserved", ctypes.c_uint * 8)]


_libc = ctypes.CDLL("/usr/lib/libSystem.B.dylib", use_errno=True)
_waitid = _libc.waitid
_waitid.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(_SignalInfo), ctypes.c_int]
_waitid.restype = ctypes.c_int
_listpids = _libc.proc_listpids
_listpids.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p, ctypes.c_int]
_listpids.restype = ctypes.c_int
_pidinfo = _libc.proc_pidinfo
_pidinfo.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int]
_pidinfo.restype = ctypes.c_int


def observe_exit(pid: int) -> tuple[int | None, int | None] | None:
    """Observe an owned child without reaping; ECHILD is unknown ownership."""
    info = _SignalInfo()
    # P_PID=1; WEXITED=4, WNOHANG=1, WNOWAIT=32 (Darwin, not Linux values).
    result = _waitid(1, pid, ctypes.byref(info), 4 | 1 | 32)
    if result != 0:
        error = ctypes.get_errno()
        if error == errno.ECHILD:
            return (None, None)
        if error == errno.EINTR:
            return None
        raise OSError(error, "Darwin waitid failed")
    if info.pid == 0:
        return None
    if info.pid != pid:
        return (None, None)
    if info.code == 1:  # CLD_EXITED
        return (info.status, None)
    if info.code in (2, 3):  # CLD_KILLED, CLD_DUMPED
        return (None, info.status)
    return (None, None)


def group_members(leader: int) -> list[int] | None:
    """Inventory live group members; unreadable/truncated snapshots are unknown.

    Once the leader is observed exited, it cannot fork. Other live members can
    fork during cleanup, so the supervisor repeats this check after signalling.
    Zombie descendants do not retain executable code or pipes.
    """
    pids = (ctypes.c_int * 16384)()
    capacity = ctypes.sizeof(pids)
    ctypes.set_errno(0)
    length = _listpids(2, leader, pids, capacity)  # PROC_PGRP_ONLY
    if length <= 0 or length >= capacity or length % ctypes.sizeof(ctypes.c_int):
        return None  # The unreaped leader must still occur in this inventory.
    members = []
    for pid in pids[:length // ctypes.sizeof(ctypes.c_int)]:
        if pid <= 0 or pid == leader:
            continue
        info = _ShortInfo()
        ctypes.set_errno(0)
        count = _pidinfo(pid, 13, 0, ctypes.byref(info), ctypes.sizeof(info))
        if count != ctypes.sizeof(info):
            if ctypes.get_errno() == errno.ESRCH:
                continue  # A member retired between the two observations.
            return None
        if info.pid != pid:
            return None
        if info.group == leader and info.status != 5:  # SZOMB
            members.append(pid)
    return members
