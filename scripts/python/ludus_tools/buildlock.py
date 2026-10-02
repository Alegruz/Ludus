"""Cooperative per-build-tree lock shared by the CLI and the Editor (P10).

Both the installed CLI and the Editor adapter take the SAME lock file,
``<build_dir>/.cmake/.ludus-editor.lock``, with a non-blocking exclusive
``flock``. This is why a CLI build and an Editor build of the same tree contend
and the second one fails with ``Busy`` instead of corrupting the tree. Raw
``cmake`` invocations outside the tooling do not honor this lock; that is a
documented unsupported case.

The lock name is intentionally identical to ``editor_tool.BuildTreeLock`` so the
two backends interlock. Keep them in sync.
"""

from __future__ import annotations

from pathlib import Path

from .errors import BUSY, ToolingError

LOCK_RELPATH = ".cmake/.ludus-editor.lock"


class BuildTreeLock:
    def __init__(self, build_dir: Path) -> None:
        self._build_dir = Path(build_dir)
        self._handle = None

    def acquire(self) -> None:
        import fcntl

        lock_dir = self._build_dir / ".cmake"
        lock_dir.mkdir(parents=True, exist_ok=True)
        handle = (lock_dir / ".ludus-editor.lock").open("a")
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            handle.close()
            raise ToolingError(
                BUSY,
                "another CLI or Editor instance is using this build tree; concurrent operations "
                "on the same tree are not supported",
            ) from exc
        self._handle = handle

    def release(self) -> None:
        if self._handle is None:
            return
        import fcntl

        try:
            fcntl.flock(self._handle, fcntl.LOCK_UN)
        except OSError:
            pass
        try:
            self._handle.close()
        except OSError:
            pass
        self._handle = None

    def __enter__(self) -> "BuildTreeLock":
        self.acquire()
        return self

    def __exit__(self, *_exc) -> None:
        self.release()
