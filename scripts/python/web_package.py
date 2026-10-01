"""Deterministic packaging of the pinned, single-threaded W6 smoke app."""
from __future__ import annotations

import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PRESET = "web-emscripten-release"
RUNTIME = ("index.html", "index.js", "index.wasm")
# Preserve notices for the linked C/C++ runtime as well as JS/WebGPU glue.
SDK_NOTICES = {
    "LICENSE": "Emscripten.txt",
    "AUTHORS": "Emscripten-authors.txt",
    "system/lib/libc/musl/COPYRIGHT": "musl.txt",
    "system/lib/libcxx/LICENSE.TXT": "libcxx.txt",
    "system/lib/libcxxabi/LICENSE.TXT": "libcxxabi.txt",
    "system/lib/compiler-rt/LICENSE.TXT": "compiler-rt.txt",
}
PORT_NOTICES = {"webgpu/src/LICENSE": "emdawnwebgpu.txt"}
NOTICE = """Ludus browser smoke demo
Ludus: MIT, see licenses/Ludus.txt.
Emscripten JS glue and Emdawnwebgpu C bridge: MIT/NCSA, see their licenses.
WebGPU native API definitions: BSD-3-Clause, see licenses/webgpu-native.txt.
Linked C/C++ runtime: see musl, libcxx, libcxxabi and compiler-rt notices.
The Emdawnwebgpu C++ wrapper, native Vulkan/Wayland and Catch2 do not ship.
Toolchain versions, payload hashes and memory policy are in build-info.json.
"""


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_leb(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    for shift in range(0, 35, 7):
        byte = data[offset]
        offset += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, offset
    raise ValueError("Invalid wasm section length")


def validate_wasm(data: bytes) -> None:
    if data[:8] != b"\0asm\x01\0\0\0":
        raise ValueError("Invalid wasm module")
    offset = 8
    memory_found = False
    while offset < len(data):
        section = data[offset]
        size, start = read_leb(data, offset + 1)
        end = start + size
        if end > len(data):
            raise ValueError("Truncated wasm section")
        if section == 0:
            length, name_start = read_leb(data, start)
            name = data[name_start:name_start + length]
            if name == b"name" or name.startswith((b".debug", b"sourceMappingURL")):
                raise ValueError("Release wasm contains debug data")
        if section == 5:
            count, position = read_leb(data, start)
            flags, position = read_leb(data, position)
            initial, position = read_leb(data, position)
            maximum, position = read_leb(data, position)
            if count != 1 or flags != 1 or initial != 512 or maximum != 4096 or position != end:
                raise ValueError("Unexpected wasm memory policy")
            memory_found = True
        offset = end
    if not memory_found:
        raise ValueError("Missing wasm memory declaration")


class References(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.files: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        for key, value in attrs:
            if key == "src" or (tag == "link" and key == "href"):
                if value:
                    self.files.append(value)


def validate_payload(files: dict[str, bytes], root: Path) -> None:
    for name, data in files.items():
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or "\\" in name:
            raise ValueError("Unsafe archive path: " + name)
        if name in RUNTIME:
            # Check actual checkout roots plus common machine-path prefixes.
            for forbidden in (str(root).encode(), b"/home/", b"/Users/", b"/workspace/", b"C:\\", b"D:\\"):
                if forbidden in data:
                    raise ValueError("Developer path in " + name)
    validate_wasm(files["index.wasm"])
    js = files["index.js"]
    if b"sourceMappingURL=" in js or b"[W6:" in js:
        raise ValueError("Debug/test code in production JS")
    if b"index.wasm" not in js:
        raise ValueError("Expected relative wasm reference is missing")
    for name in ("index.html", "iframe.html"):
        refs = References()
        refs.feed(files[name].decode("utf-8"))
        if not refs.files or any(ref not in files for ref in refs.files):
            raise ValueError("Non-local or missing HTML asset in " + name)
    # itch.io HTML archive limits, checked 2026-10-01; use decimal MB conservatively.
    if len(files) > 1000 or sum(map(len, files.values())) > 500_000_000:
        raise ValueError("Package exceeds itch.io HTML limits")
    if any(len(name) > 240 or len(data) > 200_000_000 for name, data in files.items()):
        raise ValueError("Package entry exceeds itch.io HTML limits")


def collect(root: Path) -> dict[str, bytes]:
    build = root / "out/build" / PRESET / "apps/smoke"
    sdk = root / "out/host-tools/emsdk/upstream/emscripten"
    port = sdk / "cache/ports/emdawnwebgpu/emdawnwebgpu_pkg"
    files = {name: (build / name).read_bytes() for name in RUNTIME}
    files["iframe.html"] = (root / "apps/smoke/iframe.html").read_bytes()
    files["licenses/Ludus.txt"] = (root / "LICENSE").read_bytes()
    for source, name in SDK_NOTICES.items():
        files["licenses/" + name] = (sdk / source).read_bytes()
    for source, name in PORT_NOTICES.items():
        files["licenses/" + name] = (port / source).read_bytes()
    # The pinned C API header carries a separate complete BSD notice.
    header = (port / "webgpu/include/webgpu/webgpu.h").read_bytes()
    notice, separator, _ = header.partition(b"#ifndef WEBGPU_H_")
    if not separator or b"BSD 3-Clause License" not in notice:
        raise ValueError("Pinned WebGPU native notice is missing")
    files["licenses/webgpu-native.txt"] = notice
    files["NOTICE.txt"] = NOTICE.encode()
    info = {
        "preset": PRESET,
        "toolchain": json.loads((root / "config/web_toolchain.json").read_text()),
        "memory": {"initial_bytes": 33554432, "maximum_bytes": 268435456,
                   "stack_bytes": 65536, "growth": True, "threads": False},
        "debug_symbols": False,
        "sha256": {name: digest(data) for name, data in sorted(files.items())},
    }
    files["build-info.json"] = (json.dumps(info, indent=2, sort_keys=True) + "\n").encode()
    validate_payload(files, root)
    return files


def write_archive(destination: Path, files: dict[str, bytes]) -> None:
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = 0o100644 << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data, compresslevel=9)


def verify_archive(path: Path, expected: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if sorted(names) != sorted(expected) or archive.testzip() is not None:
            raise ValueError("Archive entry set or CRC mismatch")
        for name, data in expected.items():
            if archive.read(name) != data:
                raise ValueError("Archive payload mismatch: " + name)
        # Extract only the already-validated exact entry set into an empty directory.
        with tempfile.TemporaryDirectory(prefix="ludus-web-extract-") as temporary:
            archive.extractall(temporary)
            for name, data in expected.items():
                if (Path(temporary) / name).read_bytes() != data:
                    raise ValueError("Extracted payload mismatch: " + name)


def package(root: Path) -> Path:
    files = collect(root)
    packages = root / "out/packages"
    packages.mkdir(parents=True, exist_ok=True)
    destination = packages / "ludus-web-smoke-release.zip"
    with tempfile.TemporaryDirectory(prefix="web-package-", dir=packages) as temporary:
        staging = Path(temporary) / "stage"
        staging.mkdir()
        for name, data in files.items():
            target = staging / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        archive = Path(temporary) / destination.name
        write_archive(archive, files)
        verify_archive(archive, files)
        stage = packages / "ludus-web-smoke"
        if stage.exists():
            shutil.rmtree(stage)
        shutil.move(str(staging), stage)
        archive.replace(destination)
    return destination


def main() -> int:
    if len(sys.argv) != 1:
        print("Usage: ./scripts/package-web (builds the pinned Release smoke app)", file=sys.stderr)
        return 2
    try:
        subprocess.run([str(ROOT / "scripts/build"), PRESET], cwd=ROOT, check=True)
        archive = package(ROOT)
        print(f"{archive}\nSHA256 {digest(archive.read_bytes())}")
        return 0
    except (OSError, ValueError, IndexError, subprocess.CalledProcessError) as error:
        print(f"Web package: {error}", file=sys.stderr)
        return 1
