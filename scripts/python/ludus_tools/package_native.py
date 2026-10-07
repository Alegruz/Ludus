"""Static Linux x64 payload checks; never execute a packaged binary or ldd."""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from .release_model import fail

# Declared external ABI baseline, not a sweep of the producer machine. GPU and
# window-system libraries must be bundled or gain a separately reviewed policy.
# Thanks to the Linux man-pages project, ld.so(8), Synopsis/Description:
# https://man7.org/linux/man-pages/man8/ld.so.8.html . The declared glibc x64
# interpreter is also a system prerequisite when emitted as a direct DT_NEEDED
# dependency (e.g. the provider's PIC static link closure). Keep other names closed.
SYSTEM_LIBRARIES = frozenset({
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1",
    "ld-linux-x86-64.so.2", "libstdc++.so.6", "libgcc_s.so.1", "libc++.so.1", "libc++abi.so.1", "libunwind.so.1",
})
POLICY = "linux-x64-release-1"


def validate_native(root: Path, entry_point: str) -> list[str]:
    exe = shutil.which("readelf")
    if exe is None:
        fail("readelf is required for native package validation", "MissingTools")
    entry = root / entry_point
    if not entry.is_file() or not os.access(entry, os.X_OK):
        fail("native entry point is missing or not executable", "InvalidPackage")
    with entry.open("rb") as handle:
        if handle.read(4) != b"\x7fELF":
            fail("native entry point must be an ELF executable", "InvalidPackage")
    external = set()
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        with path.open("rb") as handle:
            if handle.read(4) != b"\x7fELF":
                continue
        env = {k: v for k, v in os.environ.items() if k != "BUTLER_API_KEY"}
        env["LC_ALL"] = "C"
        with tempfile.TemporaryFile() as output:
            try:
                result = subprocess.run([exe, "-h", "-l", "-d", "-S", "--wide", str(path)],
                                        stdout=output, stderr=subprocess.DEVNULL,
                                        env=env, timeout=30, check=False)
            except (OSError, subprocess.TimeoutExpired):
                fail("native inspection failed or timed out", "InvalidPackage")
            if result.returncode or output.tell() > 8 * 1024 * 1024:
                fail(f"cannot inspect ELF file {path.relative_to(root)}", "InvalidPackage")
            output.seek(0)
            report = output.read(8 * 1024 * 1024).decode("utf-8", errors="replace")
        if "ELF64" not in report or "Advanced Micro Devices X86-64" not in report:
            fail("native payload contains a non-x64 ELF", "InvalidPackage")
        if re.search(r"\.(?:debug|zdebug)[._]", report):
            fail("native release contains debug sections", "InvalidPackage")
        interpreter = re.findall(r"Requesting program interpreter: ([^\]]+)\]", report)
        if interpreter and interpreter != ["/lib64/ld-linux-x86-64.so.2"]:
            fail("unsupported native ELF interpreter", "InvalidPackage")
        runpaths = re.findall(r"\((?:RUNPATH|RPATH)\).*?\[([^\]]*)\]", report)
        search = []
        for runpath in runpaths:
            for part in runpath.split(":"):
                if not (part == "$ORIGIN" or part.startswith("$ORIGIN/")):
                    fail("runtime search paths must be package-relative $ORIGIN paths", "InvalidPackage")
                directory = (path.parent / part.removeprefix("$ORIGIN").lstrip("/")).resolve()
                if not directory.is_relative_to(root.resolve()):
                    fail("runtime search path escapes package", "InvalidPackage")
                search.append(directory)
        for needed in re.findall(r"\(NEEDED\).*?\[([^\]]*)\]", report):
            if "/" in needed:
                fail("ELF dependency contains a producer path", "InvalidPackage")
            bundled = [directory / needed for directory in search if (directory / needed).is_file()]
            if bundled:
                with bundled[0].open("rb") as handle:
                    if handle.read(4) != b"\x7fELF":
                        fail("bundled runtime dependency is not ELF", "InvalidPackage")
                continue
            if needed not in SYSTEM_LIBRARIES:
                fail(f"unresolved runtime dependency {needed!r}; install it with a relative loader path",
                     "InvalidPackage")
            external.add(needed)
    if not (root / "NOTICE.txt").is_file() or not (root / "NOTICE.txt").stat().st_size:
        fail("package must install a nonempty NOTICE.txt", "InvalidPackage")
    licenses = root / "licenses"
    if not licenses.is_dir() or not any(p.is_file() and p.stat().st_size for p in licenses.rglob("*")):
        fail("package must install nonempty license files under licenses/", "InvalidPackage")
    return sorted(external)


def executable_identity(path: Path) -> dict:
    """Fingerprint allocated ELF sections, allowing CMake's RPATH edits.

    CMake can rewrite .dynamic and .dynstr during installation without changing
    the program. Compare the remaining allocated sections and entry address;
    the installed loader metadata is independently checked by validate_native.
    This is a consistency check for trusted install rules, not authentication.
    """
    import hashlib
    import struct

    with path.open("rb") as handle:
        header = handle.read(64)
        if len(header) != 64 or header[:6] != b"\x7fELF\x02\x01":
            fail("entry artifact must be a little-endian ELF64 file", "InvalidPackage")
        offset = struct.unpack_from("<Q", header, 40)[0]
        stride, count, names_index = struct.unpack_from("<HHH", header, 58)
        size = path.stat().st_size
        if stride != 64 or not 1 <= count <= 4096 or names_index >= count or offset + count * stride > size:
            fail("invalid ELF section table", "InvalidPackage")
        handle.seek(offset)
        table = [struct.unpack("<IIQQQQIIQQ", handle.read(64)) for _ in range(count)]
        names_header = table[names_index]
        if names_header[5] > 1024 * 1024 or names_header[4] + names_header[5] > size:
            fail("invalid ELF section names", "InvalidPackage")
        handle.seek(names_header[4])
        names = handle.read(names_header[5])
        sections = {}
        for item in table:
            if not item[2] & 2:  # SHF_ALLOC: actual program sections.
                continue
            if item[0] >= len(names) or b"\0" not in names[item[0]:]:
                fail("invalid ELF section name", "InvalidPackage")
            name = names[item[0]:].split(b"\0", 1)[0].decode("ascii")
            if name in sections:
                fail("duplicate allocated ELF section", "InvalidPackage")
            if name in (".dynamic", ".dynstr"):
                continue
            digest = hashlib.sha256()
            if item[1] != 8:  # SHT_NOBITS has no on-disk contents.
                if item[4] + item[5] > size:
                    fail("ELF section exceeds file", "InvalidPackage")
                handle.seek(item[4])
                remaining = item[5]
                while remaining:
                    block = handle.read(min(1024 * 1024, remaining))
                    if not block:
                        fail("truncated ELF section", "InvalidPackage")
                    digest.update(block)
                    remaining -= len(block)
            sections[name] = (item[1], item[2], item[3], item[5], digest.hexdigest())
        if ".text" not in sections:
            fail("ELF executable has no text section", "InvalidPackage")
        return {"entry": struct.unpack_from("<Q", header, 24)[0], "sections": sections}
