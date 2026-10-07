"""Closed S2 dependency catalog, paired compiler, strict analysis and immutable keys.

Thanks to Roblox Corporation, Luau Sandbox/Bytecode documentation and the pinned
CLI/compiler. Dependencies are declared asset identities, never discovered by
executing initializers. See docs/architecture/luau-s2.md for the trust boundary.
"""
import hashlib
import json
from pathlib import Path
import re
import runpy
import subprocess

GEN = runpy.run_path(str(Path(__file__).with_name("generate.py")), run_name="generator")
require, closed, unique = (GEN[key] for key in ("require", "closed", "unique"))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def tokens(text):
    """Lex comments/strings so import-looking text cannot manufacture dependencies."""
    result, index = [], 0
    while index < len(text):
        start = index
        if text[index].isspace():
            index += 1
            continue
        comment = text.startswith("--", index)
        if comment:
            index += 2
        long = re.match(r"\[(=*)\[", text[index:])
        if long:
            end = text.find("]" + long[1] + "]", index + len(long[0]))
            require(end >= 0, "unterminated long string/comment")
            index = end + len(long[1]) + 2
            if not comment: result.append(("string", text[start:index], start, index))
            continue
        if comment:
            end = text.find("\n", index)
            index = len(text) if end < 0 else end + 1
            continue
        if text[index] in "\"'":
            quote = text[index]
            index += 1
            while index < len(text) and text[index] != quote:
                index += 2 if text[index] == "\\" else 1
            require(index < len(text), "unterminated string")
            index += 1
            result.append(("string", text[start:index], start, index))
            continue
        word = re.match(r"[A-Za-z_][A-Za-z0-9_]*", text[index:])
        if word:
            index += len(word[0])
            result.append(("word", word[0], start, index))
        else:
            index += 1
            result.append(("symbol", text[start:index], start, index))
    return result


def imports(text):
    scanned = tokens(text)
    result = []
    for index, token in enumerate(scanned):
        if token[:2] != ("word", "require"): continue
        require(index + 3 < len(scanned) and (index == 0 or scanned[index-1][1] not in (".", ":")) and
                scanned[index+1][1] == "(" and scanned[index+2][0] == "string" and
                scanned[index+3][1] == ")", "require must use one literal asset identity")
        literal = scanned[index+2]
        identity = literal[1][1:-1]
        require(re.fullmatch(r"[0-9a-f]{16}", identity) and int(identity, 16), "invalid import identity")
        result.append((identity, literal[2], literal[3]))
    return result


def load(path):
    require(path.stat().st_size <= 65536, "manifest too large")
    data = json.loads(path.read_text(), object_pairs_hook=unique)
    closed(data, ["version", "packages"])
    require(type(data["version"]) is int and data["version"] == 1, "unsupported catalog")
    require(type(data["packages"]) is list and 1 <= len(data["packages"]) <= 8, "catalog capacity")
    names = set()
    for package in data["packages"]:
        closed(package, ["name", "revision", "schema", "state_max", "programs"])
        GEN["name"](package["name"])
        require(package["name"] not in names, "duplicate package")
        names.add(package["name"])
        for key in ("revision", "schema", "state_max"): GEN["integer"](package[key])
        require(package["schema"] <= 2 and package["state_max"] <= 10, "unsupported migration schema/range")
        require(type(package["programs"]) is list and 1 <= len(package["programs"]) <= 8, "program capacity")
        assets = {}
        for program in package["programs"]:
            closed(program, ["asset", "source", "entrypoint", "dependencies"])
            require(type(program["asset"]) is str and re.fullmatch(r"[0-9a-f]{16}", program["asset"]) and
                    int(program["asset"], 16) and program["asset"] not in assets, "duplicate/invalid asset")
            require(type(program["source"]) is str and re.fullmatch(r"[a-z][a-z0-9_]*\.luau", program["source"]), "unsafe source path")
            require(type(program["entrypoint"]) is bool, "invalid entrypoint")
            require(type(program["dependencies"]) is list and len(program["dependencies"]) <= 8 and
                    all(type(d) is str for d in program["dependencies"]) and
                    len(set(program["dependencies"])) == len(program["dependencies"]), "invalid dependencies")
            source = path.parent / program["source"]
            require(source.is_file() and source.resolve().parent == path.parent.resolve() and
                    source.stat().st_size <= 65536, "missing/unsafe/oversized source")
            require(set(identity for identity, *_ in imports(source.read_text())) == set(program["dependencies"]),
                    "source/manifest import mismatch")
            assets[program["asset"]] = program
        order, visiting = [], set()
        def visit(asset):
            require(asset in assets, "missing import")
            require(asset not in visiting, "dependency cycle")
            if asset in order: return
            visiting.add(asset)
            for dependency in assets[asset]["dependencies"]:
                require(dependency in assets and not assets[dependency]["entrypoint"], "missing/non-module import")
                visit(dependency)
            visiting.remove(asset)
            order.append(asset)
        for asset in sorted(assets): visit(asset)
        require(sum(p["entrypoint"] for p in assets.values()) == 2, "this cohort requires two entrypoints")
        package["programs"] = [assets[asset] for asset in order]
    return data


def cook(manifest, output, compiler, analyzer, pin, definitions, contract_hash, tool_key, cache=None):
    data = load(manifest)
    output.mkdir(parents=True, exist_ok=True)
    header = '#pragma once\n#include "session.h"\nnamespace ludus::s2 {\n'
    records = []
    for package in data["packages"]:
        directory = output / package["name"]
        directory.mkdir(exist_ok=True)
        analysis = directory / "analysis"
        analysis.mkdir(exist_ok=True)
        programs, keys, maps = [], {}, {}
        for program in package["programs"]:
            source = (manifest.parent / program["source"]).read_text()
            view = source
            for asset, start, end in reversed(imports(source)):
                view = view[:start] + '"./' + asset + '"' + view[end:]
            (analysis / (program["asset"] + ".luau")).write_text(definitions + view)
        for program in package["programs"]:
            analyzed = analysis / (program["asset"] + ".luau")
            subprocess.run([str(analyzer), "--mode=strict", str(analyzed)], check=True, timeout=30)
            source = (manifest.parent / program["source"]).read_text()
            compiled = directory / (program["asset"] + ".luau")
            compiled.write_text(definitions + source)
            key = sha(json.dumps({"semantic": source, "deps": {d: keys[d] for d in program["dependencies"]},
                "pin": pin, "tools": tool_key, "contract": contract_hash, "profile": "interpreter-debug-o1-g2", "schema": package["schema"]},
                sort_keys=True, separators=(",", ":")).encode())
            cached = cache / package["name"] / (key + ".bytecode") if cache else None
            if cached is not None and cached.is_file():
                bytecode = cached.read_bytes()
            else:
                bytecode = subprocess.run([str(compiler), "--binary", "-O1", "-g2", str(compiled)],
                                          check=True, stdout=subprocess.PIPE, timeout=30).stdout
            require(bytecode and bytecode[0] != 0 and len(bytecode) <= 262144, "compiler artifact invalid")
            keys[program["asset"]] = key
            (directory / (key + ".bytecode")).write_bytes(bytecode)
            label = package["name"].upper() + "_" + program["asset"].upper()
            header += f"inline constexpr foundation::uint8 {label}[] = {{" + ",".join(str(b) for b in bytecode) + "};\n"
            dependencies = ",".join("0x" + d + "ULL" for d in program["dependencies"])
            programs.append(f'{{ .Asset=0x{program["asset"]}ULL, .Revision={package["revision"]}, .Code={label}, .Bytes=sizeof({label}), .Source="@{program["asset"]}", .Dependencies={{{dependencies}}}, .DependencyCount={len(program["dependencies"])}, .Entrypoint={str(program["entrypoint"]).lower()} }}')
            first = definitions.count("\n") + 1
            maps[program["asset"]] = {"file": program["source"], "first_compiled_line": first,
                "source_lines": source.count("\n"), "source_sha256": sha(source.encode()), "program_key": key}
        key = sha(json.dumps({"programs": keys, "revision": package["revision"], "schema": package["schema"],
                             "state_max": package["state_max"]}, sort_keys=True).encode())
        name = package["name"].upper()
        header += f"inline constexpr runtime::scripting::Program {name}_PROGRAMS[] = {{" + ",".join(programs) + "};\n"
        header += f'inline constexpr Package {name}{{ .Key="{key}", .Contract="{contract_hash}", .Pin="{pin}", .Revision={package["revision"]}, .Schema={package["schema"]}, .StateMax={package["state_max"]}, .Programs={name}_PROGRAMS, .Count=sizeof({name}_PROGRAMS)/sizeof({name}_PROGRAMS[0]), .FirstLine={definitions.count(chr(10))+1} }};\n'
        record = {"key": key, "pin": pin, "contract": contract_hash, "revision": package["revision"],
                  "schema": package["schema"], "program_keys": keys, "maps": maps,
                  "bytecode": {p.name: sha(p.read_bytes()) for p in directory.glob("*.bytecode")}}
        (directory / "maps.json").write_text(json.dumps(maps, sort_keys=True, indent=2) + "\n")
        (directory / "bundle.json").write_text(json.dumps(record, sort_keys=True, indent=2) + "\n")
        records.append(record)
    header += "}\n"
    (output / "packages.h").write_text(header)
    return records
