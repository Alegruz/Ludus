"""Native release packaging and offline publication planning for host tools."""
from __future__ import annotations

import ctypes
import errno
import os
import re
import subprocess
import tempfile
import zipfile
from dataclasses import asdict, replace
from pathlib import Path
from types import SimpleNamespace

from . import __version__, descriptor, operations
from .buildlock import BuildTreeLock
from .package_native import POLICY, validate_native
from .package_verify import MAX_MANIFEST, file_digest, inventory, verify_package
from .release_model import canonical, fail, load_release, sha256, string
from .resolve import assert_stamp_unchanged, write_stamp
from .sdkstore import SdkStore


def _version(value: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.+-]{0,127}", string(value, "version", 128)):
        fail("version must be an alphanumeric release label")
    return value


def _source_state(root: Path) -> dict:
    env = {key: value for key, value in os.environ.items() if key != "BUTLER_API_KEY"}
    try:
        revision = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"], capture_output=True,
                                  env=env, timeout=10, check=False)
        dirty = subprocess.run(["git", "-C", str(root), "status", "--porcelain", "--untracked-files=normal"],
                               capture_output=True, env=env, timeout=10, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return {"revision": "unknown", "dirty": True}
    text = revision.stdout.decode("ascii", errors="replace").strip()
    if revision.returncode or dirty.returncode or not re.fullmatch(r"[a-f0-9]{40,64}", text):
        return {"revision": "unknown", "dirty": True}
    return {"revision": text, "dirty": bool(dirty.stdout)}


def _publish_directory(staging: Path, destination: Path) -> None:
    """Linux atomic no-replace rename; never expose an incomplete directory."""
    libc = ctypes.CDLL(None, use_errno=True)
    rename = getattr(libc, "renameat2", None)
    if rename is None:
        fail("atomic package publication requires Linux renameat2", "MissingTools")
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(staging), -100, os.fsencode(destination), 1) != 0:
        if ctypes.get_errno() == errno.EEXIST:
            fail("package destination already exists", "DestinationExists")
        fail(f"cannot atomically publish package: {os.strerror(ctypes.get_errno())}", "GenerationFailed")


def _archive(payload: Path, archive: Path, records: list[dict], manifest: dict) -> None:
    metadata = canonical(manifest)
    if len(metadata) > MAX_MANIFEST:
        fail("payload manifest exceeds size limit", "InvalidPackage")
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as writer:
        for record in sorted([*records, {"path": "build-info.json", "mode": 0o644}], key=lambda r: r["path"]):
            info = zipfile.ZipInfo(record["path"], date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100000 | record["mode"]) << 16
            if record["path"] == "build-info.json":
                writer.writestr(info, metadata, compresslevel=9)
            else:
                with (payload / record["path"]).open("rb") as src, writer.open(info, "w", force_zip64=True) as dst:
                    while block := src.read(1024 * 1024):
                        dst.write(block)


def package_project(project: Path, *, profile: str, version: str,
                    store: SdkStore, sdk: Path | None = None) -> Path:
    """Build, install, validate and atomically publish an exact Release package."""
    version = _version(version)
    paths = operations.locate_project(project)
    root = paths.project_dir.resolve()
    config = load_release(root)
    if profile not in config.profiles:
        fail(f"unknown release profile {profile!r}")
    selected = config.profiles[profile]
    resolved = operations.resolve_project(project, store=store, cli_sdk_prefix=sdk, profile="linux-clang-release")
    if resolved.resolution.identity.target_triple != "x86_64-linux-gnu":
        fail("SDK target does not match release profile", "SdkIncompatible")
    if resolved.resolution.identity.engine_version != resolved.descriptor.engine.version:
        fail("SDK engine version does not match project requirement", "SdkIncompatible")
    resolved.descriptor = replace(resolved.descriptor, target=selected.target, preset="linux-clang-release")
    lock_digest = file_digest(paths.lock_path)
    descriptor_digest = file_digest(paths.descriptor_path)
    source = _source_state(root)
    cmake = operations._cmake_executable()
    env = operations._sdk_env(resolved.resolution)
    package_parent = root / "out" / "packages" / profile
    package_parent.mkdir(parents=True, exist_ok=True)
    with BuildTreeLock(resolved.paths.build_dir), tempfile.TemporaryDirectory(prefix=".ludus-release-", dir=package_parent) as temp:
        from cmake_targets import query_codemodel, target_executable, verify_identity

        shim = SimpleNamespace(EngineError=ValueError)
        query_codemodel(resolved.paths.build_dir, "ludus-cli")
        # Configure explicitly each time: CMake/source/preset edits also matter.
        for argv in (
            operations.configure_argv(cmake, "linux-clang-release", resolved.paths.source_dir, resolved.paths.build_dir),
            operations.build_argv(cmake, resolved.paths.build_dir, selected.target),
        ):
            if operations._run(argv, cwd=resolved.paths.source_dir, env=env) != 0:
                fail("release build failed; refusing to package a stale executable", "BuildFailed")
            assert_stamp_unchanged(resolved.paths.build_dir, resolved.resolution)
        write_stamp(resolved.paths.build_dir, resolved.resolution)
        try:
            verify_identity(resolved.paths.build_dir, resolved.paths.source_dir, shim, client="ludus-cli")
            artifact = target_executable(resolved.paths.build_dir, selected.target, shim, client="ludus-cli")
        except ValueError as exc:
            fail(str(exc), "BuildFailed")
        if not artifact.is_file() or not os.access(artifact, os.X_OK):
            fail("post-build artifact is missing or not executable", "BuildFailed")
        payload = Path(temp) / "payload"
        payload.mkdir()
        install = [cmake, "--install", str(resolved.paths.build_dir), "--config", "Release",
                   "--component", selected.install_component, "--prefix", str(payload)]
        if operations._run(install, cwd=resolved.paths.source_dir, env=env) != 0:
            fail("release installation failed", "BuildFailed")
        records = inventory(payload)
        external = validate_native(payload, selected.entry_point)
        if file_digest(payload / selected.entry_point) != file_digest(artifact):
            fail("installed entry point differs from the selected CMake artifact", "InvalidPackage")
        # Policy prohibits source/build/SDK machine paths in the actual payload.
        needles = [str(p.resolve()).encode("utf-8") for p in (root, resolved.paths.source_dir,
                    resolved.paths.build_dir, resolved.resolution.prefix)]
        for record in records:
            with (payload / record["path"]).open("rb") as handle:
                tail = b""
                while block := handle.read(1024 * 1024):
                    data = tail + block
                    if any(needle in data for needle in needles):
                        fail(f"producer path in payload {record['path']!r}", "InvalidPackage")
                    tail = data[-max(map(len, needles)):]
        if (load_release(root).digest != config.digest or file_digest(paths.lock_path) != lock_digest
                or file_digest(paths.descriptor_path) != descriptor_digest or _source_state(root) != source):
            fail("project inputs changed during packaging", "Conflict")
        assert_stamp_unchanged(resolved.paths.build_dir, resolved.resolution)
        manifest = {"schemaVersion": 1, "profile": profile, "targetPlatform": selected.target_platform,
                    "entryPoint": selected.entry_point, "version": version, "source": source,
                    "engineLockSha256": lock_digest, "releaseConfigSha256": config.digest,
                    "sdk": asdict(resolved.resolution.identity), "localInputs": source["dirty"] or resolved.resolution.is_override,
                    "policy": POLICY, "systemLibraries": external, "files": records}
        completed = Path(temp) / "package"
        completed.mkdir()
        archive = completed / "game.zip"
        _archive(payload, archive, records, manifest)
        archive_digest = file_digest(archive)
        sidecar = {"schemaVersion": 1, "archiveSha256": archive_digest,
                   "manifestSha256": sha256(canonical(manifest)), "profile": profile, "version": version,
                   "releaseConfigSha256": config.digest, "toolVersion": __version__}
        (completed / "package.json").write_bytes(canonical(sidecar))
        (completed / "validation.json").write_bytes(canonical({
            "schemaVersion": 1, "archiveSha256": archive_digest, "policy": POLICY,
            "toolVersion": __version__, "checks": ["payload", "native", "clean-extraction"],
        }))
        verify_package(completed)
        destination = package_parent / archive_digest
        # Lock package publication independently of the build tree in case
        # two valid projects share a chosen output profile.
        with BuildTreeLock(package_parent):
            if destination.exists() or destination.is_symlink():
                if destination.is_symlink() or verify_package(destination) != verify_package(completed):
                    fail("existing package conflicts with generated package", "Conflict")
                return destination
            _publish_directory(completed, destination)
        return destination


def publish_plan(project: Path, package: Path, *, destination: str, allow_local_inputs: bool = False) -> dict:
    """Offline plan only: never resolve an SDK, execute a game or authenticate."""
    paths = operations.locate_project(project)
    model = descriptor.parse_descriptor_file(paths.descriptor_path)
    if model.version != 2 or model.provider != "cmake":
        fail("publishing requires a version-2 cmake project; migrate explicitly")
    from .lockfile import parse_lock_file, validate_descriptor_lock_agreement
    if model.engine is None:
        fail("project has no engine requirement")
    validate_descriptor_lock_agreement(model.engine.version, parse_lock_file(paths.lock_path))
    config = load_release(paths.project_dir)
    if destination not in config.destinations or config.itch_target is None:
        fail(f"unknown itch.io destination {destination!r}")
    verified = verify_package(package)
    manifest = verified["manifest"]
    profile, channel = config.destinations[destination]
    if manifest["sdk"]["engine_version"] != model.engine.version:
        fail("package SDK engine version differs from project requirement", "Conflict")
    if (verified["releaseConfigSha256"] != config.digest or verified["profile"] != profile
            or manifest["engineLockSha256"] != file_digest(paths.lock_path)):
        fail("package does not match current release configuration/profile/lock", "Conflict")
    if manifest["localInputs"] and not allow_local_inputs:
        fail("package used dirty/unknown sources or an SDK override; pass --allow-local-inputs to inspect its upload plan")
    return {"operation": "offline-upload-plan", "destination": destination,
            "target": config.itch_target, "channel": channel, "version": verified["version"],
            "archiveSha256": verified["archiveSha256"], "localInputs": manifest["localInputs"],
            "expandedBytes": sum(record["size"] for record in manifest["files"]),
            "argv": ["butler", "push", "<verified-private-snapshot>/game.zip",
                     f"{config.itch_target}:{channel}", "--userversion", verified["version"]],
            "prerequisites": ["live uploader and pinned butler setup are not implemented",
                              "existing itch.io project with appropriate visibility and authentication"]}
