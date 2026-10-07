"""Explicit itch.io upload of a verified private package snapshot; no retries."""
from __future__ import annotations

import hashlib
import os
import selectors
import shutil
import signal
import subprocess
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path

from .buildlock import BuildTreeLock
from .package_verify import file_digest
from .release import publish_plan
from .release_model import canonical, fail
from .errors import ToolingError

BUTLER_VERSION = "15.31.0"
BUTLER_URL = f"https://broth.itch.zone/butler/linux-amd64/{BUTLER_VERSION}/archive/default"
BUTLER_ARCHIVE_SHA256 = "4f2a3f22b12f870923504d4b6935535cad377b45859f5fe9419e3adc0611a48c"
BUTLER_BINARY_SHA256 = "578e1ebe8548ddf2a1b8374d5a85c0308668df3c06a2a1b9edb6ad1112c606eb"
MAX_DOWNLOAD = 128 * 1024 * 1024


def install_butler(destination: Path) -> Path:
    """Install only the pinned executable into a caller-owned private directory."""
    archive = destination / "butler.zip"
    digest = hashlib.sha256()
    count = 0
    with urllib.request.urlopen(BUTLER_URL, timeout=30) as source, archive.open("xb") as output:
        if not source.geturl().startswith("https://"):
            fail("butler download redirected outside HTTPS", "MissingTools")
        while block := source.read(1024 * 1024):
            count += len(block)
            if count > MAX_DOWNLOAD:
                fail("butler download exceeds size limit", "MissingTools")
            digest.update(block)
            output.write(block)
    if digest.hexdigest() != BUTLER_ARCHIVE_SHA256:
        fail("butler archive digest mismatch", "MissingTools")
    binary = destination / "butler"
    with zipfile.ZipFile(archive) as reader:
        if reader.getinfo("butler").file_size > MAX_DOWNLOAD:
            fail("oversized butler executable", "MissingTools")
        with reader.open("butler") as source, binary.open("xb") as output:
            shutil.copyfileobj(source, output, 1024 * 1024)
    if file_digest(binary) != BUTLER_BINARY_SHA256:
        fail("butler executable digest mismatch", "MissingTools")
    binary.chmod(0o700)
    archive.unlink()
    return binary


def _run_upload(argv: list[str], env: dict[str, str], secret: str) -> int:
    """Bounded streamed diagnostics, redacted even across read boundaries."""
    import sys
    child = subprocess.Popen(argv, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, start_new_session=True)
    pending = b""
    token = secret.encode()
    deadline = time.monotonic() + 15 * 60
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(child.stdout, selectors.EVENT_READ)
            while True:
                if time.monotonic() >= deadline:
                    fail("upload timed out; remote outcome may be unknown", "UploadFailed")
                events = selector.select(timeout=0.1)
                if not events:
                    continue
                data = os.read(child.stdout.fileno(), 4096)
                if not data:
                    sys.stdout.buffer.write(pending.replace(token, b"[redacted]"))
                    sys.stdout.buffer.flush()
                    break
                pending += data
                # Replace complete occurrences, retaining a possible partial
                # occurrence at the chunk boundary before emitting diagnostics.
                pending = pending.replace(token, b"[redacted]")
                keep = min(len(token) - 1, len(pending))
                boundary = len(pending) - keep
                sys.stdout.buffer.write(pending[:boundary])
                sys.stdout.buffer.flush()
                pending = pending[boundary:]
        while time.monotonic() < deadline:
            info = os.waitid(os.P_PID, child.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
            if info is not None:
                return info.si_status if info.si_code == os.CLD_EXITED else 128 + info.si_status
            time.sleep(0.01)
        fail("upload timed out; remote outcome may be unknown", "UploadFailed")
    finally:
        # Terminate ordinary descendants on completion/failure/interrupt too.
        try:
            os.killpg(child.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        # The leader remains waitable, so its PID cannot be reused during
        # descendant cleanup. Kill remaining ordinary group members, then reap.
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        child.wait(timeout=2)
        child.stdout.close()


def upload(project: Path, package: Path, *, destination: str, allow_local_inputs: bool = False,
           expected_digest: str | None = None, target_override: str | None = None) -> dict:
    """Caller must explicitly authorize this command; it can update a live page."""
    import sys
    if sys.platform != "linux":
        fail("live itch.io upload transport requires Linux; macOS supports packaging and offline plans", "UnsupportedReleaseTarget")
    root = project.parent if project.is_file() else project
    root = root.resolve()
    # Serialize uploads for this project. CI also serializes the repository's
    # upload job; no newer local push can overtake an older one in this process.
    with BuildTreeLock(root / "out/publishing"), tempfile.TemporaryDirectory(prefix="ludus-upload-") as temp:
        private = Path(temp)
        snapshot = private / "snapshot"
        snapshot.mkdir(mode=0o700)
        for name in ("game.zip", "package.json", "validation.json"):
            src = package / name
            if src.is_symlink() or not src.is_file():
                fail("upload source is missing or linked", "InvalidPackage")
            shutil.copyfile(src, snapshot / name)
        plan = publish_plan(project, snapshot, destination=destination, allow_local_inputs=allow_local_inputs, target_override=target_override)
        if expected_digest is not None and plan["archiveSha256"] != expected_digest:
            fail("upload snapshot differs from approved package digest", "Conflict")
        # Auth enters only after offline validation and tool acquisition. Never
        # pass the key to CMake, the game, package validation or a compiler.
        secret = os.environ.get("BUTLER_API_KEY", "")
        if not secret or len(secret.encode()) > 4096 or any(c.isspace() for c in secret):
            fail("BUTLER_API_KEY is required for noninteractive uploading", "AuthenticationRequired")
        binary = install_butler(private)
        env = {"PATH": os.defpath, "HOME": str(private), "XDG_CONFIG_HOME": str(private / "config"),
               "BUTLER_API_KEY": secret, "LANG": "C.UTF-8"}
        argv = [str(binary), "push", str(snapshot / "game.zip"), f"{plan['target']}:{plan['channel']}",
                "--userversion", plan["version"]]
        failure = None
        code = None
        try:
            code = _run_upload(argv, env, secret)
        except (OSError, ToolingError, KeyboardInterrupt) as exc:
            failure = exc
        receipt = {"schemaVersion": 1, "outcome": "upload-submitted" if code == 0 else "upload-outcome-unknown" if failure else "upload-failed",
                   "archiveSha256": plan["archiveSha256"], "version": plan["version"], "target": plan["target"],
                   "channel": plan["channel"], "butlerVersion": BUTLER_VERSION, "exitCode": code}
        receipts = root / "out/publish-receipts"
        receipts.mkdir(parents=True, exist_ok=True)
        # A new attempt writes a separate receipt, without changing its package.
        receipt_path = receipts / f"{plan['archiveSha256']}-{time.time_ns()}.json"
        receipt_path.write_bytes(canonical(receipt))
        if failure is not None:
            raise failure
        if code:
            fail("butler upload failed; inspect receipt/logs before retrying", "UploadFailed")
        return {**receipt, "receipt": str(receipt_path)}
