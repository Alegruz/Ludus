"""Bounded archive verification and canonical payload inventory."""
from __future__ import annotations

import hashlib
import os
import stat
import zipfile
from pathlib import Path

from .package_native import POLICY, validate_native
from .release_model import fields, json_bytes, payload_path, sha256, string, fail, DIGEST

MAX_ENTRIES = 10_000
MAX_EXPANDED = 30_000_000_000
MAX_MANIFEST = 4 * 1024 * 1024
MAX_CENTRAL_DIRECTORY = 8 * 1024 * 1024
FORBIDDEN_SUFFIXES = {".a", ".o", ".h", ".hpp", ".pdb", ".rdi", ".dSYM"}


def file_digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def _check_path(name: str, seen: set[str]) -> None:
    payload_path(name)
    folded = name.casefold()
    if folded in seen:
        fail(f"duplicate/case-colliding payload path {name!r}", "InvalidPackage")
    seen.add(folded)
    if any(Path(part).suffix in FORBIDDEN_SUFFIXES for part in Path(name).parts) or name.startswith(("include/", "share/Ludus/", ".ludus/")):
        fail(f"authoring or debug file in player payload: {name!r}", "InvalidPackage")


def inventory(root: Path) -> list[dict]:
    records, seen, total = [], set(), 0
    for directory, dirs, files in os.walk(root, followlinks=False):
        for name in sorted(dirs + files):
            path = Path(directory) / name
            rel = path.relative_to(root).as_posix()
            payload_path(rel)
            status = path.lstat()
            if not (stat.S_ISREG(status.st_mode) or stat.S_ISDIR(status.st_mode)):
                fail(f"non-regular package entry {rel!r}", "InvalidPackage")
            if rel.casefold() in seen:
                fail("case-colliding package entries", "InvalidPackage")
            # Directories participate in collision detection but are omitted
            # from the ZIP; extraction creates only validated parent directories.
            if stat.S_ISDIR(status.st_mode):
                seen.add(rel.casefold())
                continue
            _check_path(rel, seen)
            if rel == "build-info.json":
                fail("build-info.json is reserved for the packager", "InvalidPackage")
            total += status.st_size
            if total > MAX_EXPANDED or len(records) >= MAX_ENTRIES - 1:
                fail("payload exceeds native packaging limits", "InvalidPackage")
            records.append({"path": rel, "size": status.st_size,
                            "mode": 0o755 if status.st_mode & 0o111 else 0o644,
                            "sha256": file_digest(path)})
    return sorted(records, key=lambda record: record["path"])


def extract_archive(archive: Path, destination: Path, *, run_command=None, env=None, cancel_check=None) -> dict:
    """Extract into a caller-owned empty directory, validating before writing."""
    if any(destination.iterdir()):
        fail("verification extraction directory must be empty", "InvalidPackage")
    try:
        # Bound ZIP metadata before ZipFile allocates its central directory.
        # CPython's parser handles both ordinary EOCD and ZIP64 records without
        # reading the central directory. This private API is covered on every
        # supported Python in CI; replace it if its contract changes.
        with archive.open("rb") as handle:
            end = zipfile._EndRecData(handle)
        if (end is None or end[zipfile._ECD_ENTRIES_TOTAL] > MAX_ENTRIES
                or end[zipfile._ECD_SIZE] > MAX_CENTRAL_DIRECTORY):
            fail("ZIP central directory exceeds policy limits", "InvalidPackage")
        with zipfile.ZipFile(archive) as reader:
            entries = reader.infolist()
            if not 1 <= len(entries) <= MAX_ENTRIES:
                fail("invalid ZIP entry count", "InvalidPackage")
            seen, total, directories = set(), 0, set()
            parent_spellings = {}
            for item in entries:
                _check_path(item.filename, seen)
                kind = stat.S_IFMT(item.external_attr >> 16)
                mode = stat.S_IMODE(item.external_attr >> 16)
                if (kind != stat.S_IFREG or mode not in (0o644, 0o755) or item.flag_bits & 1
                        or item.compress_type != zipfile.ZIP_DEFLATED):
                    fail("ZIP contains unsupported types/modes/compression", "InvalidPackage")
                total += item.file_size
                if total > MAX_EXPANDED:
                    fail("ZIP exceeds expanded size limit", "InvalidPackage")
                for parent in Path(item.filename).parents:
                    if parent == Path("."):
                        continue
                    spelling = parent.as_posix()
                    folded = spelling.casefold()
                    if parent_spellings.setdefault(folded, spelling) != spelling:
                        fail("ZIP has case-colliding parent directories", "InvalidPackage")
                    directories.add(folded)
            if seen & directories:
                fail("ZIP path collides with a parent directory", "InvalidPackage")
            for item in entries:
                output = destination / item.filename
                output.parent.mkdir(parents=True, exist_ok=True)
                size = 0
                with reader.open(item) as src, output.open("xb") as dst:
                    while block := src.read(1024 * 1024):
                        size += len(block)
                        if size > item.file_size:
                            fail("ZIP entry exceeds declared size", "InvalidPackage")
                        dst.write(block)
                if size != item.file_size:
                    fail("ZIP entry size mismatch", "InvalidPackage")
                output.chmod(stat.S_IMODE(item.external_attr >> 16))
    except (zipfile.BadZipFile, RuntimeError, OSError, NotImplementedError) as exc:
        fail(f"invalid package ZIP: {exc}", "InvalidPackage")
    manifest_path = destination / "build-info.json"
    try:
        with manifest_path.open("rb") as handle:
            manifest = json_bytes(handle.read(MAX_MANIFEST + 1), "build-info.json", maximum=MAX_MANIFEST)
    except OSError as exc:
        fail(f"missing build-info.json: {exc}", "InvalidPackage")
    fields(manifest, {"schemaVersion", "profile", "targetPlatform", "entryPoint", "version", "source",
                      "engineLockSha256", "releaseConfigSha256", "sdk", "localInputs", "policy",
                      "systemLibraries", "files"}, set(), "build-info.json")
    from .package_web import WEB_POLICY, validate_web
    web = manifest["targetPlatform"] == "web"
    from .package_macos import POLICY as MAC_POLICY, validate_native as validate_macos
    macos = manifest["targetPlatform"] in ("macos-arm64", "macos-x64")
    policy = WEB_POLICY if web else MAC_POLICY if macos else POLICY
    if (type(manifest["schemaVersion"]) is not int or manifest["schemaVersion"] != 1
            or manifest["policy"] != policy or manifest["targetPlatform"] not in ("linux-x64", "macos-arm64", "macos-x64", "web")
            or type(manifest["localInputs"]) is not bool):
        fail("unsupported package manifest/policy", "InvalidPackage")
    payload_path(manifest["entryPoint"])
    from .release_model import identifier
    import re
    identifier(manifest["profile"], "package profile")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.+-]{0,127}", string(manifest["version"], "version", 128)):
        fail("invalid package version label", "InvalidPackage")
    for key in ("engineLockSha256", "releaseConfigSha256"):
        if not isinstance(manifest[key], str) or not DIGEST.fullmatch(manifest[key]):
            fail(f"invalid {key}", "InvalidPackage")
    fields(manifest["source"], {"revision", "dirty"}, set(), "source")
    source = manifest["source"]
    if (type(source["dirty"]) is not bool or not isinstance(source["revision"], str)
            or not (source["revision"] == "unknown" or re.fullmatch(r"[a-f0-9]{40,64}", source["revision"]))
            or ((source["dirty"] or source["revision"] == "unknown") and not manifest["localInputs"])):
        fail("invalid or inconsistent source provenance", "InvalidPackage")
    from dataclasses import fields as dataclass_fields
    from .identity import SdkIdentity
    fields(manifest["sdk"], {field.name for field in dataclass_fields(SdkIdentity)}, set(), "sdk")
    triples = {"web": "wasm32-unknown-emscripten", "linux-x64": "x86_64-linux-gnu", "macos-arm64": "arm64-apple-darwin", "macos-x64": "x86_64-apple-darwin"}
    if (manifest["sdk"]["target_triple"] != triples[manifest["targetPlatform"]] or manifest["sdk"]["flavor"] != "Release"
            or manifest["sdk"]["cxx_standard"] != "C++23"):
        fail("package SDK is not a supported native Release SDK", "InvalidPackage")
    if macos and (manifest["sdk"]["cxx_runtime_abi"] != "libc++" or manifest["sdk"]["distro_baseline"] != "macos-14.0"):
        fail("macOS package SDK must declare libc++ and the macOS 14.0 baseline", "InvalidPackage")
    records = manifest["files"]
    if not isinstance(records, list) or len(records) != len(entries) - 1:
        fail("manifest inventory does not match ZIP", "InvalidPackage")
    records_seen = set()
    for record in records:
        fields(record, {"path", "size", "mode", "sha256"}, set(), "inventory entry")
        _check_path(record["path"], records_seen)
        if (record["path"] == "build-info.json" or type(record["size"]) is not int
                or record["size"] < 0 or type(record["mode"]) is not int
                or record["mode"] not in (0o644, 0o755) or not isinstance(record["sha256"], str)
                or not DIGEST.fullmatch(record["sha256"])):
            fail("invalid inventory entry", "InvalidPackage")
        path = destination / record["path"]
        if (not path.is_file() or path.stat().st_size != record["size"]
                or stat.S_IMODE(path.stat().st_mode) != record["mode"]
                or file_digest(path) != record["sha256"]):
            fail(f"payload hash/size/mode mismatch: {record['path']}", "InvalidPackage")
    if records_seen != seen - {"build-info.json"}:
        fail("manifest inventory differs from ZIP", "InvalidPackage")
    external = validate_macos(destination, manifest["entryPoint"], manifest["targetPlatform"], runner=run_command, env=env, cancel_check=cancel_check) if macos else (validate_web if web else validate_native)(destination, manifest["entryPoint"])
    if external != manifest["systemLibraries"]:
        fail("system prerequisite inventory mismatch", "InvalidPackage")
    return manifest


def verify_package(package_dir: Path, *, run_command=None, env=None, cancel_check=None) -> dict:
    import tempfile
    from .release_model import read_json

    for name in ("game.zip", "package.json", "validation.json"):
        if (package_dir / name).is_symlink() or not (package_dir / name).is_file():
            fail(f"missing or linked package file {name}", "InvalidPackage")
    sidecar = read_json(package_dir / "package.json")
    fields(sidecar, {"schemaVersion", "archiveSha256", "manifestSha256", "profile", "version",
                     "releaseConfigSha256", "toolVersion"}, set(), "package.json")
    if (type(sidecar["schemaVersion"]) is not int or sidecar["schemaVersion"] != 1
            or file_digest(package_dir / "game.zip") != sidecar["archiveSha256"]):
        fail("package archive digest/schema mismatch", "InvalidPackage")
    report = read_json(package_dir / "validation.json")
    fields(report, {"schemaVersion", "archiveSha256", "policy", "toolVersion", "checks"}, set(), "validation.json")
    from .package_web import WEB_POLICY
    from .package_macos import POLICY as MAC_POLICY
    platform_check = "web" if report["policy"] == WEB_POLICY else "native"
    if (type(report["schemaVersion"]) is not int or report["schemaVersion"] != 1
            or report["archiveSha256"] != sidecar["archiveSha256"] or report["policy"] not in (POLICY, WEB_POLICY, MAC_POLICY)
            or report["checks"] != ["payload", platform_check, "clean-extraction"]):
        fail("validation report does not describe this package", "InvalidPackage")
    with tempfile.TemporaryDirectory(prefix="ludus-package-verify-") as temp:
        manifest = extract_archive(package_dir / "game.zip", Path(temp), run_command=run_command, env=env, cancel_check=cancel_check)
        if manifest["policy"] != report["policy"]:
            fail("validation policy disagrees with payload", "InvalidPackage")
        if sha256((Path(temp) / "build-info.json").read_bytes()) != sidecar["manifestSha256"]:
            fail("package manifest digest mismatch", "InvalidPackage")
    for key in ("profile", "version", "releaseConfigSha256"):
        if sidecar[key] != manifest[key]:
            fail("package sidecar disagrees with payload metadata", "InvalidPackage")
    return {**sidecar, "manifest": manifest}
