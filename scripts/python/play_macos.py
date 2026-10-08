"""Bounded thin Mach-O UUID and detached DWARF checks for native live play."""
from __future__ import annotations
import struct
from pathlib import Path

# Thanks to Apple, XNU EXTERNAL_HEADERS/mach-o/loader.h, mach_header_64,
# uuid_command and section_64: these layouts inform this independent reader.
# https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/loader.h
# Thanks to LLVM, dsymutil command guide, --flat and --out: use a standalone
# MH_DSYM image so generations retain symbols without mutable object-file maps.
# https://llvm.org/docs/CommandGuide/dsymutil.html


def inspect(path: Path) -> dict:
    from play_probe import ProbeError
    size = path.stat().st_size
    if not 32 <= size <= 512 * 1024 * 1024:
        raise ProbeError("Mach-O artifact exceeds 512 MiB or is truncated")
    with path.open("rb") as source:
        header = source.read(32)
        if len(header) != 32:
            raise ProbeError("Mach-O artifact changed during inspection")
        magic, cpu, subtype, kind, count, length, flags, reserved = struct.unpack("<8I", header)
        if magic != 0xfeedfacf or cpu not in (0x1000007, 0x100000c) or kind not in (2, 6, 8, 10):
            raise ProbeError("expected a thin macOS ARM64/x64 Mach-O executable/module/symbol image")
        if not 1 <= count <= 4096 or length > 8 * 1024 * 1024 or 32 + length > size:
            raise ProbeError("invalid Mach-O command bounds")
        commands = source.read(length)
        if len(commands) != length:
            raise ProbeError("Mach-O artifact changed during inspection")
        offset, identity, symbols = 0, None, False
        sections = set()
        for _ in range(count):
            if offset + 8 > length:
                raise ProbeError("truncated Mach-O command")
            command, stride = struct.unpack_from("<II", commands, offset)
            if stride < 8 or stride % 8 or offset + stride > length:
                raise ProbeError("invalid Mach-O command size")
            data = commands[offset:offset + stride]
            if command == 0x1b:
                if stride != 24 or identity is not None:
                    raise ProbeError("invalid or duplicate Mach-O UUID")
                identity = data[8:24].hex()
                if identity == "0" * 32:
                    raise ProbeError("invalid zero Mach-O UUID")
            elif command == 0x19:
                if stride < 72:
                    raise ProbeError("truncated Mach-O segment")
                number = struct.unpack_from("<I", data, 64)[0]
                if len(sections) + number > 4096 or 72 + number * 80 != stride:
                    raise ProbeError("invalid Mach-O section bounds")
                for index in range(number):
                    section = struct.unpack_from("<16s16sQQIIIIIIII", data, 72 + index * 80)
                    name, segment = section[0].rstrip(b"\0"), section[1].rstrip(b"\0")
                    key = (segment, name)
                    if key in sections:
                        raise ProbeError("duplicate Mach-O section")
                    sections.add(key)
                    if section[8] & 0xff not in (1, 0xc, 0x12):
                        position, amount = section[4], section[3]
                        # MH_DSYM preserves original code addresses/sizes with offset zero;
                        # only its DWARF sections carry file bytes.
                        virtual_code = kind == 10 and segment != b"__DWARF" and position == 0
                        if amount and not virtual_code and (position < 32 + length or position + amount > size):
                            raise ProbeError("Mach-O section outside artifact")
                        if key == (b"__DWARF", b"__debug_info") and amount:
                            symbols = True
            offset += stride
        if offset != length or identity is None:
            raise ProbeError("Mach-O artifact must carry exactly one UUID")
        return dict(build_id=identity, cpu=cpu, subtype=subtype, kind=kind, embedded_symbols=symbols)


def identity(path: Path, symbols: Path | None = None) -> dict:
    from play_probe import ProbeError
    image = inspect(path)
    if image["kind"] not in (2, 6, 8):
        raise ProbeError("symbol image cannot be activated as gameplay")
    if symbols is not None:
        debug = inspect(symbols)
        if debug["kind"] != 10 or any(debug[key] != image[key] for key in ("build_id", "cpu", "subtype")):
            raise ProbeError("Mach-O symbol UUID/architecture disagrees with artifact")
        if not debug["embedded_symbols"]:
            raise ProbeError("Mach-O symbol image has no DWARF debug information")
    elif not image["embedded_symbols"]:
        raise ProbeError("Mach-O artifact requires matching detached DWARF symbols")
    return {**image, "embedded_symbols": symbols is None,
            "matching_symbols": True, "artifact_type": "module" if image["kind"] in (6, 8) else "host"}
