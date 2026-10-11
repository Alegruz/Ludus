"""Reconcile first-party compilation commands with CMake's independent source list."""
from __future__ import annotations

from collections import Counter
import json
from pathlib import Path
import shlex
from types import SimpleNamespace

import cmake_targets


SOURCE_ROOTS = ("modules", "apps", "tools", "tests", "examples")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".m", ".mm"}


def request_codemodel(build: Path) -> None:
    cmake_targets.query_codemodel(build, "ludus-quality")


def read_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError(f"Cannot read quality input {path}: {error}") from error


def compilation_entries(build: Path) -> list[dict]:
    # Thanks to LLVM, "JSON Compilation Database Format Specification": file
    # names are relative to directory, and one file can have several commands.
    # https://clang.llvm.org/docs/JSONCompilationDatabase.html
    database = read_json(build / "compile_commands.json")
    if not isinstance(database, list):
        raise ValueError("Compilation database must contain a list")
    entries = []
    seen = set()
    for entry in database:
        if not isinstance(entry, dict) or not isinstance(entry.get("directory"), str) \
                or not isinstance(entry.get("file"), str) or not entry["file"]:
            raise ValueError("Malformed compilation database entry: directory/file required")
        directory = Path(entry["directory"])
        if not directory.is_absolute():
            raise ValueError("Compilation database directories must be absolute")
        arguments = entry.get("arguments")
        if arguments is None and isinstance(entry.get("command"), str):
            arguments = shlex.split(entry["command"])
        if not isinstance(arguments, list) or not arguments \
                or not all(isinstance(arg, str) for arg in arguments):
            raise ValueError("Malformed compilation database command/arguments")
        source = (directory / entry["file"]).resolve()
        # LLVM expands response files while loading the database, before it
        # applies this entry's working directory. Relocated per-variant databases
        # must retain their original response-file base (CMake .modmap files).
        normalized_arguments = []
        for argument in arguments:
            if argument.startswith("@"):
                response = (directory / argument[1:]).resolve()
                if not response.is_file():
                    raise ValueError(f"Missing compilation response file: {response}; build analysis inputs first")
                argument = "@" + str(response)
            normalized_arguments.append(argument)
        normalized = {"directory": str(directory.resolve()), "file": str(source),
                      "arguments": normalized_arguments}
        if "output" in entry:
            if not isinstance(entry["output"], str):
                raise ValueError("Compilation database output must be a string")
            normalized["output"] = entry["output"]
        key = json.dumps(normalized, sort_keys=True)
        if key not in seen:
            entries.append(normalized)
            seen.add(key)
    return sorted(entries, key=lambda entry: (entry["file"], json.dumps(entry, sort_keys=True)))


def first_party(root: Path, source: Path) -> bool:
    return source.suffix.lower() in SOURCE_SUFFIXES and any(
        source.is_relative_to(root / folder) for folder in SOURCE_ROOTS)


def expected_sources(root: Path, build: Path) -> tuple[Counter, set[Path], str]:
    sources, source_root = cmake_targets.compiled_sources(
        build, root, SimpleNamespace(EngineError=ValueError), client="ludus-quality")
    expected = Counter()
    generated = set()
    for item in sources:
        source = Path(item["path"])
        if item["generated"]:
            generated.add(source)
        elif first_party(root, source):
            expected[source] += 1
    return expected, generated, source_root


def inventory(root: Path, build: Path) -> tuple[list[dict], dict]:
    root, build = root.resolve(), build.resolve()
    expected, generated, source_root = expected_sources(root, build)
    selected = []
    excluded = []
    for entry in compilation_entries(build):
        source = Path(entry["file"])
        if source in generated:
            reason = "CMake-declared generated source"
        elif first_party(root, source):
            if not source.is_file():
                raise ValueError(f"Missing compiled first-party source: {source}")
            selected.append(entry)
            continue
        elif source.suffix.lower() not in SOURCE_SUFFIXES:
            reason = "Source language outside C/C++/Objective-C analysis"
        elif source.is_relative_to(root / "third_party") or source.is_relative_to(root / "out") \
                or not source.is_relative_to(root):
            reason = "External/prepared dependency source"
        else:
            raise ValueError(f"Unclassified compilation source ownership: {source}")
        excluded.append({"file": str(source), "reason": reason})
    actual = Counter(Path(entry["file"]) for entry in selected)
    if not expected or not actual:
        raise ValueError("Analysis has no translation units; required source inventory cannot be empty")
    mismatches = [f"{path.relative_to(root)}: expected {expected[path]}, found {actual[path]}"
                  for path in sorted(expected.keys() | actual.keys()) if expected[path] != actual[path]]
    if mismatches:
        raise ValueError("Compilation source omissions or stale commands:\n" + "\n".join(mismatches))
    report = {"version": 1, "source_root": source_root, "expected_commands": sum(expected.values()),
              "sources": [{"file": str(path.relative_to(root)), "commands": expected[path]}
                          for path in sorted(expected)], "excluded": excluded}
    return selected, report
