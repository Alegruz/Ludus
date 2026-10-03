"""Editor RAD discovery/preferences and bounded ELF symbol validation.

This module never starts a debugger or installs it. All child execution stays
in editor_tool's owned, cancellable supervisor; RAD session policy is shared
with scripts/debug through rad_debugger.debug_session.
"""
from __future__ import annotations

import struct
from pathlib import Path

import rad_debugger as rad
from ludus_tools.errors import ToolingError
from ludus_tools.project_setup import read_object, write_object


def resolve_debugger(root: Path, preference: Path, engine, *, override: str | None = None,
                     managed: bool = False) -> Path:
    settings = read_object(preference) if not override and not managed else {}
    if settings:
        if (settings.get("provider") != "rad" or not isinstance(settings.get("managed", False), bool)
                or not isinstance(settings.get("executable", ""), str)):
            raise ToolingError("InvalidProject", "Invalid local debugger settings; fix .ludus/debugger.json")
        override = settings.get("executable") or None
        managed = settings.get("managed", False)
    try:
        if managed:
            binary = rad.managed_binary(root, engine)
            if binary is None:
                raise engine.EngineError("The selected managed RAD installation is missing or incomplete")
            return binary
        return rad.resolve_binary(root, engine, override)
    except engine.EngineError as exc:
        raise ToolingError("MissingDebugger", str(exc)) from exc


def save_preference(path: Path, binary: Path, *, managed: bool) -> None:
    # Tool paths are machine-local, including for legacy CMake/engine projects.
    ignore = path.parent.parent / ".gitignore"
    contents = ignore.read_text() if ignore.exists() else ""
    if not {"/.ludus/", ".ludus/"} & set(contents.splitlines()):
        ignore.write_text(contents + "\n/.ludus/\n")
    write_object(path, {"provider": "rad", "managed": managed,
                        "executable": "" if managed else str(binary)})


def validate_symbols(artifact: Path) -> None:
    """Inspect ELF64 section names with bounded reads; never execute the game."""
    def invalid(message):
        return ToolingError("ArtifactInvalid", message)
    with artifact.open("rb") as stream:
        size = artifact.stat().st_size
        header = stream.read(64)
        if len(header) != 64 or header[:6] != b"\x7fELF\x02\x01":
            raise invalid("RAD debugging requires a native Linux x64 ELF executable")
        elf = struct.unpack("<16sHHIQQQIHHHHHH", header)
        offset, entry_size, count, names_index = elf[6], elf[11], elf[12], elf[13]
        if (elf[2] != 62 or entry_size != 64 or count == 0 or names_index >= count
                or offset < 64 or offset + count * entry_size > size):
            raise invalid("Invalid or unsupported ELF section table")
        stream.seek(offset)
        sections = [struct.unpack("<IIQQQQIIQQ", stream.read(64)) for _ in range(count)]
        names_section = sections[names_index]
        names_offset, names_size = names_section[4], names_section[5]
        if names_size > 2 * 1024 * 1024 or names_offset + names_size > size:
            raise invalid("Invalid ELF section-name table")
        stream.seek(names_offset)
        strings = stream.read(names_size)
        names = set()
        for section in sections:
            name_offset = section[0]
            end = strings.find(b"\0", name_offset)
            if name_offset >= len(strings) or end < 0:
                raise invalid("Invalid ELF section name")
            if section[1] == 1 and section[5] > 0:
                if section[4] + section[5] > size:
                    raise invalid("ELF data section extends beyond the executable")
                names.add(strings[name_offset:end])
        if not {b".debug_info", b".zdebug_info"} & names:
            raise invalid("The game has no DWARF symbols; build it with a compatible Debug/Development SDK")
        if b".eh_frame_hdr" not in names:
            raise invalid("RAD needs .eh_frame_hdr for stack unwinding; link the game with --eh-frame-hdr")
