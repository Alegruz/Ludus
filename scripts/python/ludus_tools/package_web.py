"""Release-only Emscripten package builds using project-owned CMake presets."""
from __future__ import annotations

import os
import re
import tempfile
from dataclasses import asdict
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit
from types import SimpleNamespace

from . import __version__, descriptor, operations
from .buildlock import BuildTreeLock
from .identity import load_prefix_manifest, MANIFEST_RELPATH
from .lockfile import parse_lock_file, validate_descriptor_lock_agreement
from .package_verify import file_digest, inventory, verify_package
from .release_model import canonical, fail, load_release, payload_path, sha256

WEB_POLICY = "ludus-web-release-1"


def _uleb(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    for shift in range(0, 35, 7):
        if offset >= len(data):
            fail("truncated WebAssembly section", "InvalidPackage")
        byte = data[offset]
        offset += 1
        value |= (byte & 127) << shift
        if not byte & 128:
            return value, offset
    fail("invalid WebAssembly section length", "InvalidPackage")


def _validate_wasm(path: Path) -> None:
    # Read section headers and custom-section names only; never load the whole
    # module or instantiate/execute WebAssembly during verification.
    with path.open("rb") as handle:
        if handle.read(8) != b"\x00asm\x01\x00\x00\x00":
            fail("invalid WebAssembly header", "InvalidPackage")
        end = path.stat().st_size
        while handle.tell() < end:
            section = handle.read(1)[0]
            encoded = bytearray()
            for _ in range(5):
                byte = handle.read(1)
                if not byte:
                    fail("truncated WebAssembly section", "InvalidPackage")
                encoded.extend(byte)
                if not byte[0] & 128:
                    break
            length, _ = _uleb(bytes(encoded), 0)
            next_section = handle.tell() + length
            if section > 13 or next_section > end:
                fail("invalid WebAssembly section", "InvalidPackage")
            if section == 0:
                prefix = handle.read(min(length, 512))
                name_length, start = _uleb(prefix, 0)
                if name_length > len(prefix) - start:
                    fail("oversized WebAssembly custom-section name", "InvalidPackage")
                name = prefix[start:start + name_length].decode("utf-8", "replace")
                if name == "name" or name.startswith(".debug") or name in ("sourceMappingURL", "external_debug_info"):
                    fail("debug section in browser payload", "InvalidPackage")
            handle.seek(next_section)


class _References(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.assets: list[str] = []

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        key = "src" if tag in ("script", "img", "audio", "video", "source", "iframe") else "href" if tag == "link" else None
        if key and key in attributes:
            self.assets.append(attributes[key])
        if tag == "base":
            fail("HTML base URLs are unsupported in self-contained packages", "InvalidPackage")


def validate_web(root: Path, entry_point: str) -> list[str]:
    if entry_point != "index.html":
        fail("browser package requires root index.html", "InvalidPackage")
    entry = root / entry_point
    if not entry.is_file() or entry.stat().st_size > 2 * 1024 * 1024:
        fail("missing or oversized browser entry point", "InvalidPackage")
    parser = _References()
    parser.feed(entry.read_text(encoding="utf-8"))
    for asset in parser.assets:
        url = urlsplit(asset)
        if url.scheme or url.netloc or url.path.startswith("/"):
            fail("HTML runtime assets must use local relative URLs", "InvalidPackage")
        name = payload_path(unquote(url.path))
        if not (root / name).is_file():
            fail(f"missing HTML runtime asset {name!r}", "InvalidPackage")
    wasm = list(root.rglob("*.wasm"))
    if not wasm or not list(root.rglob("*.js")):
        fail("Emscripten payload requires JavaScript and WebAssembly", "InvalidPackage")
    for path in root.rglob("*"):
        if path.is_file():
            with path.open("rb") as handle:
                if handle.read(4) == b"\x7fELF":
                    fail("native binary in browser payload", "InvalidPackage")
            if path.suffix.lower() in (".map", ".spv", ".slang", ".cpp", ".c", ".so", ".dll", ".exe"):
                fail("authoring/native/debug file in browser payload", "InvalidPackage")
            if path.suffix == ".wasm":
                _validate_wasm(path)
    if not (root / "NOTICE.txt").is_file() or not any(p.is_file() for p in (root / "licenses").rglob("*")):
        fail("browser package requires notices and license files", "InvalidPackage")
    return []


def _configured_sdk(build: Path):
    cache = build / "CMakeCache.txt"
    value = next((line.split("=", 1)[1] for line in cache.read_text().splitlines()
                  if line.startswith("Ludus_DIR:") and "=" in line), "")
    if value:
        for prefix in (Path(value), *Path(value).parents):
            if (prefix / MANIFEST_RELPATH).is_file():
                return prefix, load_prefix_manifest(prefix)
    fail("configured browser SDK has no identity manifest", "SdkIncompatible")


def package_web(project: Path, *, profile: str, version: str, sdk: Path | None = None, run_command=None, cancel_check=None) -> Path:
    from .release import _archive, _publish_directory, _source_state

    paths = operations.locate_project(project)
    root = paths.project_dir.resolve()
    model = descriptor.parse_descriptor_file(paths.descriptor_path)
    if model.version != 2 or model.provider != "cmake" or model.engine is None:
        fail("browser packaging requires a version-2 CMake project")
    validate_descriptor_lock_agreement(model.engine.version, parse_lock_file(paths.lock_path))
    config = load_release(root)
    selected = config.profiles[profile]
    source_dir = (root / model.source_dir).resolve()
    build = source_dir / "out/build" / selected.configure_preset
    source = _source_state(root)
    descriptor_digest, lock_digest = file_digest(paths.descriptor_path), file_digest(paths.lock_path)
    runner = run_command or operations._run
    cmake = operations._cmake_executable()
    env = {k: v for k, v in os.environ.items() if k != "BUTLER_API_KEY"}
    parent = root / "out/packages" / profile
    parent.mkdir(parents=True, exist_ok=True)
    with BuildTreeLock(build), tempfile.TemporaryDirectory(prefix=".ludus-web-", dir=parent) as temp:
        from cmake_targets import query_codemodel, target_executable, verify_identity
        shim = SimpleNamespace(EngineError=ValueError)
        query_codemodel(build, "ludus-cli")
        argv = [cmake, "--preset", selected.configure_preset, "-B", str(build)]
        if sdk:
            argv.append(f"-DCMAKE_PREFIX_PATH={sdk.resolve()}")
        if runner(argv, cwd=source_dir, env=env):
            fail("browser configure failed", "BuildFailed")
        cache = (build / "CMakeCache.txt").read_text()
        if not re.search(r"^CMAKE_BUILD_TYPE:STRING=Release$", cache, re.M):
            fail("browser preset must configure Release", "InvalidRelease")
        prefix, identity = _configured_sdk(build)
        if (identity.target_triple != "wasm32-unknown-emscripten" or identity.flavor != "Release"
                or identity.engine_version != model.engine.version):
            fail("browser SDK must match engine version and wasm32 Release", "SdkIncompatible")
        sdk_digest = file_digest(prefix / MANIFEST_RELPATH)
        if runner([cmake, "--build", str(build), "--target", selected.target], cwd=source_dir, env=env):
            fail("browser build failed; refusing stale artifacts", "BuildFailed")
        payload = Path(temp) / "payload"
        payload.mkdir()
        if runner([cmake, "--install", str(build), "--config", "Release", "--component",
                            selected.install_component, "--prefix", str(payload)], cwd=source_dir, env=env):
            fail("browser install failed", "BuildFailed")
        records = inventory(payload)
        validate_web(payload, selected.entry_point)
        try:
            verify_identity(build, source_dir, shim, client="ludus-cli")
            artifact = target_executable(build, selected.target, shim, client="ludus-cli")
        except ValueError as exc:
            fail(str(exc), "BuildFailed")
        if artifact.suffix != ".js":
            fail("browser executable must produce one JavaScript artifact", "InvalidPackage")
        built_outputs = {artifact.name: artifact, artifact.with_suffix(".wasm").name: artifact.with_suffix(".wasm")}
        for name, built in built_outputs.items():
            installed = payload / name
            if not built.is_file() or not installed.is_file() or file_digest(built) != file_digest(installed):
                fail("installed browser artifact differs from selected build", "InvalidPackage")
        needles = [str(p.resolve()).encode() for p in (root, source_dir, build, prefix)]
        for record in records:
            with (payload / record["path"]).open("rb") as handle:
                tail = b""
                while block := handle.read(1024 * 1024):
                    data = tail + block
                    if any(needle in data for needle in needles):
                        fail("producer path in browser payload", "InvalidPackage")
                    tail = data[-max(map(len, needles)):]
        if (file_digest(paths.descriptor_path) != descriptor_digest or file_digest(paths.lock_path) != lock_digest
                or load_release(root).digest != config.digest or _source_state(root) != source
                or file_digest(prefix / MANIFEST_RELPATH) != sdk_digest):
            fail("browser package inputs changed", "Conflict")
        manifest = {"schemaVersion": 1, "profile": profile, "targetPlatform": "web", "entryPoint": selected.entry_point,
                    "version": version, "source": source, "engineLockSha256": lock_digest,
                    "releaseConfigSha256": config.digest, "sdk": asdict(identity), "localInputs": True,
                    "policy": WEB_POLICY, "systemLibraries": [], "files": records}
        completed = Path(temp) / "package"
        completed.mkdir()
        _archive(payload, completed / "game.zip", records, manifest)
        digest = file_digest(completed / "game.zip")
        (completed / "package.json").write_bytes(canonical({"schemaVersion": 1, "archiveSha256": digest,
            "manifestSha256": sha256(canonical(manifest)), "profile": profile, "version": version,
            "releaseConfigSha256": config.digest, "toolVersion": __version__}))
        (completed / "validation.json").write_bytes(canonical({"schemaVersion": 1, "archiveSha256": digest,
            "policy": WEB_POLICY, "toolVersion": __version__, "checks": ["payload", "web", "clean-extraction"]}))
        verify_package(completed)
        if cancel_check:
            cancel_check()
        destination = parent / digest
        with BuildTreeLock(parent):
            if destination.exists():
                if destination.is_symlink() or verify_package(destination) != verify_package(completed):
                    fail("existing browser package conflicts", "Conflict")
            else:
                _publish_directory(completed, destination)
        return destination
