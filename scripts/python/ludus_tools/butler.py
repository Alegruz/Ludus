"""Explicit, pinned butler acquisition; never executes, logs in or uploads."""
from __future__ import annotations

import hashlib
import os
import platform
import stat
import tempfile
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path

from .package_verify import file_digest
from .release_model import fail

BUTLER_VERSION = "15.31.0"
MAX_DOWNLOAD = 128 * 1024 * 1024


@dataclass(frozen=True)
class ButlerArtifact:
    """One reviewed upstream platform archive and its executable digest."""
    channel: str
    archive_sha256: str
    binary_sha256: str

    @property
    def url(self) -> str:
        return f"https://broth.itch.zone/butler/{self.channel}/{BUTLER_VERSION}/archive/default"


# Thanks to itch.io, The butler manual, "Installing butler", automation-friendly
# downloads: versioned broth channels provide ZIP archives. We install only the
# executable; optional 7-zip libraries are not needed for butler push.
# https://itch.io/docs/butler/installing.html
ARTIFACTS = {
    "linux-amd64": ButlerArtifact("linux-amd64", "4f2a3f22b12f870923504d4b6935535cad377b45859f5fe9419e3adc0611a48c", "578e1ebe8548ddf2a1b8374d5a85c0308668df3c06a2a1b9edb6ad1112c606eb"),
    "darwin-arm64": ButlerArtifact("darwin-arm64", "5a5fcd3dc83de480748223388d9b5d6eab8df786bedb5d6730ffe59df87b2c5b", "f1405fee4efdf246b0437b1d55b79e163c0e89faa548e7241a8fe0bcbb7a0701"),
    "darwin-amd64": ButlerArtifact("darwin-amd64", "70a4b8543fddee7031052ea76f1846ba071913148e20cf7a2f18421702fc1929", "54551e648ce42d350cd41404d85cb1725df8d8a811adbaf9e0e1015f725bdc8e"),
}


def host_artifact() -> ButlerArtifact:
    """Select the running host's admitted CPU; fail before any filesystem/network work."""
    system, machine = platform.system(), platform.machine().lower()
    architecture = {"x86_64": "amd64", "amd64": "amd64", "arm64": "arm64", "aarch64": "arm64"}.get(machine)
    family = {"Linux": "linux", "Darwin": "darwin"}.get(system)
    artifact = ARTIFACTS.get(f"{family}-{architecture}")
    if artifact is None:
        fail(f"pinned butler is unavailable for {system}/{machine}; supported hosts are Linux x64 and macOS arm64/x64", "MissingTools")
    return artifact


class _HttpsRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        if urllib.parse.urlsplit(new_url).scheme.lower() != "https":
            fail("butler download redirected outside HTTPS", "MissingTools")
        return super().redirect_request(request, response, code, message, headers, new_url)


def _copy_bounded(source, output) -> str:
    digest, count = hashlib.sha256(), 0
    while block := source.read(1024 * 1024):
        count += len(block)
        if count > MAX_DOWNLOAD:
            fail("butler download/executable exceeds size limit", "MissingTools")
        digest.update(block)
        output.write(block)
    return digest.hexdigest()


def install_butler(destination: Path) -> Path:
    """Download and verify the host pin, then publish destination/butler without replacing it.

    Creates the destination if missing. Existing files are preserved. Staging is
    private and removed on failure or interruption; a newly created empty destination
    is removed on failure. No PATH, shell settings, credentials or login state change.
    The caller owns the returned executable (mode 0700); this function never runs it.
    """
    artifact = host_artifact()
    destination = Path(destination).absolute()
    if destination.is_symlink() or (destination.exists() and not destination.is_dir()):
        fail("butler destination must be an ordinary directory", "Conflict")
    binary = destination / "butler"
    if os.path.lexists(binary):
        fail("butler destination already exists; choose another directory", "Conflict")
    created = False
    published = False
    try:
        try:
            destination.mkdir(parents=True, mode=0o700)
            created = True
        except FileExistsError:
            pass
        with tempfile.TemporaryDirectory(prefix=".butler-", dir=destination) as temporary:
            staging = Path(temporary)
            archive = staging / "archive.zip"
            opener = urllib.request.build_opener(_HttpsRedirect())
            with opener.open(artifact.url, timeout=30) as source, archive.open("xb") as output:
                if urllib.parse.urlsplit(source.geturl()).scheme.lower() != "https":
                    fail("butler download redirected outside HTTPS", "MissingTools")
                if _copy_bounded(source, output) != artifact.archive_sha256:
                    fail("butler archive digest mismatch", "MissingTools")
            candidate = staging / "butler"
            with zipfile.ZipFile(archive) as reader:
                entries = [entry for entry in reader.infolist() if entry.filename == "butler"]
                if len(entries) != 1:
                    fail("butler archive must contain one executable", "MissingTools")
                entry = entries[0]
                kind = stat.S_IFMT(entry.external_attr >> 16)
                if entry.is_dir() or kind not in (0, stat.S_IFREG) or entry.flag_bits & 1 or entry.file_size > MAX_DOWNLOAD:
                    fail("invalid butler executable entry", "MissingTools")
                with reader.open(entry) as source, candidate.open("xb") as output:
                    digest = _copy_bounded(source, output)
                if digest != artifact.binary_sha256 or file_digest(candidate) != artifact.binary_sha256:
                    fail("butler executable digest mismatch", "MissingTools")
            candidate.chmod(0o700)
            try:
                # A same-filesystem hard link publishes only completed verified bytes,
                # and fails if another installer/file wins the destination race.
                os.link(candidate, binary)
            except FileExistsError:
                fail("butler destination appeared during installation", "Conflict")
            published = True
        return binary
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
        fail(f"butler installation failed: {error}", "MissingTools")
    finally:
        if created and not published:
            try:
                destination.rmdir()
            except OSError:
                pass
