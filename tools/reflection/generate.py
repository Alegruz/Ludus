#!/usr/bin/env python3
"""Generate scalar reflection bindings; C++ remains authoritative for native layout.

Thanks to Allen Pouratian, 'Platform-Independent, Function-Binding Code Generator',
Game Programming Gems 3, section 1.4, pp. 38-43: generate ordinary source and let
each target compiler check types. This independently written generator reads a
closed JSON manifest, never parses C++, computes offsets, or executes snippets.
Review: docs/architecture/reflection-serialization-gems-review.md.
"""
import argparse
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import struct
import sys
import tempfile
import uuid

TYPES = {
    "bool": ("Bool", "Boolean", False, True),
    "int32": ("Int32", "Signed", -(1 << 31), (1 << 31) - 1),
    "uint32": ("Uint32", "Unsigned", 0, (1 << 32) - 1),
    "int64": ("Int64", "Signed", -(1 << 63), (1 << 63) - 1),
    "uint64": ("Uint64", "Unsigned", 0, (1 << 64) - 1),
    "float32": ("Float32", "Real", -3.4028234663852886e38, 3.4028234663852886e38),
    "float64": ("Float64", "Real", -sys.float_info.max, sys.float_info.max),
}
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z_0-9]*\Z")
QUALIFIED = re.compile(r"[A-Za-z_][A-Za-z_0-9]*(::[A-Za-z_][A-Za-z_0-9]*)*\Z")
KEY = re.compile(r"[A-Za-z][A-Za-z_0-9]*\Z")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def closed(obj, required, optional=()):
    require(isinstance(obj, dict), "expected an object")
    require(set(required) <= obj.keys(), f"missing keys: {set(required) - obj.keys()}")
    require(obj.keys() <= set(required) | set(optional), "unknown manifest keys")


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def scalar(value, kind):
    _, _, low, high = TYPES[kind]
    if kind == "bool":
        require(type(value) is bool, "bool requires true/false")
    elif kind.startswith("float"):
        require(type(value) in (int, float) and math.isfinite(value), "finite number required")
    else:
        require(type(value) is int, "integer required")
    require(low <= value <= high, f"{kind} value is out of range")
    if kind == "float32":
        value = struct.unpack("!f", struct.pack("!f", value))[0]
    return value


def load(path):
    require(path.stat().st_size <= 65536, "manifest exceeds 64 KiB")
    data = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=no_duplicates,
                      parse_constant=lambda text: require(False, f"nonfinite constant: {text}"))
    closed(data, ("schema", "version", "namespace", "name", "header", "fields"), ("reserved",))
    require(isinstance(data["schema"], str) and str(uuid.UUID(data["schema"])) == data["schema"],
            "schema must be a canonical lowercase UUID")
    require(uuid.UUID(data["schema"]).int != 0, "nil schema UUID is invalid")
    require(type(data["version"]) is int and 0 < data["version"] < 1 << 32, "invalid schema version")
    require(isinstance(data["namespace"], str) and QUALIFIED.fullmatch(data["namespace"]), "invalid namespace")
    require(isinstance(data["name"], str) and IDENTIFIER.fullmatch(data["name"]), "invalid native type name")
    require(len(data["namespace"]) <= 128 and len(data["name"]) <= 64, "native name exceeds budget")
    header = data["header"]
    require(isinstance(header, str) and re.fullmatch(r"[A-Za-z_0-9/.-]+", header), "invalid header path")
    require(not PurePosixPath(header).is_absolute() and ".." not in PurePosixPath(header).parts,
            "header must be relative to the manifest directory")
    require((path.parent / header).is_file(), f"native header not found: {header}")
    require(isinstance(data["fields"], list) and 0 < len(data["fields"]) <= 64, "expected 1..64 fields")
    reserved = data.setdefault("reserved", [])
    require(isinstance(reserved, list) and all(type(x) is int and 0 < x < 1 << 32 for x in reserved),
            "invalid reserved IDs")
    require(len(set(reserved)) == len(reserved), "duplicate reserved ID")
    ids, keys, members = set(reserved), set(), set()
    for field in data["fields"]:
        closed(field, ("id", "member", "key", "label", "type", "default", "persist", "editable"), ("min", "max"))
        fid = field["id"]
        require(type(fid) is int and 0 < fid < 1 << 32 and fid not in ids, "duplicate/reserved/invalid field ID")
        ids.add(fid)
        for name, regex, seen in (("member", IDENTIFIER, members), ("key", KEY, keys)):
            value = field[name]
            require(isinstance(value, str) and regex.fullmatch(value) and value not in seen, f"invalid/duplicate {name}")
            require(len(value.encode("utf-8")) <= 64, f"{name} exceeds 64 bytes")
            seen.add(value)
        label = field["label"]
        require(isinstance(label, str) and 0 < len(label.encode("utf-8")) <= 64 and
                all(ord(c) >= 32 for c in label), "label requires 1..64 UTF-8 bytes without controls")
        require(type(field["persist"]) is bool and type(field["editable"]) is bool, "flags must be booleans")
        kind = field["type"]
        require(isinstance(kind, str) and kind in TYPES, "unsupported scalar kind")
        for name, fallback in (("min", TYPES[kind][2]), ("max", TYPES[kind][3]), ("default", None)):
            field[name] = scalar(field.get(name, fallback), kind)
        require(field["min"] <= field["default"] <= field["max"], "invalid bounds/default")
    return data


def check_baseline(data, baseline):
    # This first slice admits one released wire version, not an implicit migration.
    require(data['schema'] == baseline['schema'] and data['version'] == baseline['version'],
            'baseline identity/version mismatch; a new version requires an explicit migration')
    def wire(document):
        result = []
        for field in document['fields']:
            if not field['persist']:
                continue
            contract = {key: field[key] for key in ('id', 'key', 'type', 'default', 'min', 'max')}
            if field['type'].startswith('float'):
                encoding = '!f' if field['type'] == 'float32' else '!d'
                for key in ('default', 'min', 'max'):
                    # Compare the declared width's bits, including signed zero.
                    contract[key] = struct.pack(encoding, field[key])
            result.append(contract)
        return result
    require(wire(data) == wire(baseline), 'released persistent schema changed')
    current = {field['id']: field for field in data['fields']}
    for field in baseline['fields']:
        if field['id'] in current:
            require(current[field['id']]['type'] == field['type'] and
                    current[field['id']]['persist'] == field['persist'] and
                    current[field['id']]['key'] == field['key'], 'released field ID changed meaning')
        else:
            require(field['id'] in data['reserved'], 'retired field ID must be reserved')
    require(set(baseline['reserved']) <= set(data['reserved']), 'released reserved ID cannot be reclaimed')


def cpp_string(text):
    # Escape bytes rather than universal characters: deterministic UTF-8 on all targets.
    return '"' + ''.join(chr(b) if 32 <= b < 127 and chr(b) not in '\\"' else f'\\{b:03o}'
                          for b in text.encode("utf-8")) + '"'


def literal(value, kind):
    if kind == "bool":
        return "true" if value else "false"
    if kind in ("int32", "int64"):
        return "(-9223372036854775807LL - 1LL)" if value == -(1 << 63) else f"{value}LL"
    if kind in ("uint32", "uint64"):
        return f"{value}ULL"
    return repr(value)


def value_expr(value, kind):
    enum, member, _, _ = TYPES[kind]
    return f"Value{{ .Type = Kind::{enum}, .{member} = {literal(value, kind)} }}"


def generate(data, header_name, schema_name):
    name, namespace = data["name"], data["namespace"]
    fields = data["fields"]
    count = len(fields)
    preamble = f"// Generated from {schema_name}; edit the manifest/native header, not this file.\n"
    header = preamble + f'''#pragma once
#include <ludus/foundation/base/types.h>
#include <ludus/foundation/serialization/json.hpp>
#include <span>
#include <string_view>
#include "{data['header']}"
namespace {namespace}
{{
inline constexpr ludus::foundation::usize k{name}FieldCount = {count};
inline constexpr ludus::foundation::uint32 k{name}SchemaVersion = {data['version']}U;
[[nodiscard]] const ludus::foundation::reflection::Schema& {name}Schema() noexcept;
[[nodiscard]] ludus::foundation::reflection::Status Read{name}(const {name}& source,
    std::span<ludus::foundation::reflection::Value> output, ludus::foundation::reflection::Diagnostic& error) noexcept;
[[nodiscard]] ludus::foundation::reflection::Status Prepare{name}(const {name}& source,
    std::span<const ludus::foundation::reflection::Edit> edits, {name}& output,
    ludus::foundation::reflection::Diagnostic& error) noexcept;
[[nodiscard]] ludus::foundation::reflection::Status Read{name}Json(std::string_view input, {name}& output,
    const ludus::foundation::AllocationDomain& domain, ludus::foundation::reflection::Diagnostic& error) noexcept;
[[nodiscard]] ludus::foundation::reflection::Status Write{name}Json(const {name}& source,
    std::span<ludus::foundation::uint8> storage, std::span<const ludus::foundation::uint8>& output,
    ludus::foundation::reflection::Diagnostic& error) noexcept;
}}
'''
    source = preamble + f'''#include "{header_name}"
#include <type_traits>
namespace {namespace}
{{
using namespace ludus::foundation;
using namespace ludus::foundation::reflection;
namespace
{{
static_assert(std::is_trivially_copyable_v<{name}> && std::is_nothrow_copy_assignable_v<{name}> &&
              std::is_nothrow_copy_constructible_v<{name}>, "v1 reflection requires a plain scalar record");
static_assert(!std::is_union_v<{name}>, "union reflection requires an explicit adapter");
static_assert(sizeof({name}) <= 4096, "v1 native candidate exceeds 4 KiB");
'''
    for index, field in enumerate(fields):
        source += f"constexpr auto kMember{index} = &{name}::{field['member']};\n"
        native = "bool" if field["type"] == "bool" else f"ludus::foundation::{field['type']}"
        source += f"static_assert(std::is_same_v<decltype({name}::{field['member']}), {native}>, \"manifest/native type mismatch\");\n"
    source += "constexpr Field kFields[] = {\n"
    for field in fields:
        source += f'''    {{ .Id = {field['id']}U, .Key = {cpp_string(field['key'])}, .Label = {cpp_string(field['label'])},
      .Default = {value_expr(field['default'], field['type'])},
      .Min = {value_expr(field['min'], field['type'])},
      .Max = {value_expr(field['max'], field['type'])},
      .Persist = {str(field['persist']).lower()}, .Editable = {str(field['editable']).lower()} }},
'''
    uid = ', '.join(f"0x{byte:02x}" for byte in uuid.UUID(data["schema"]).bytes)
    source += f'''}};
constexpr Schema kSchema{{ .Id = {{ .Bytes = {{{uid}}} }}, .Version = {data['version']}U,
    .Name = "{namespace}::{name}", .Fields = kFields }};
}} // namespace
const Schema& {name}Schema() noexcept {{ return kSchema; }}
Status Read{name}(const {name}& source, std::span<Value> output, Diagnostic& error) noexcept
{{
    const Value values[] = {{
'''
    for index, field in enumerate(fields):
        enum, member, _, _ = TYPES[field['type']]
        expression = f"source.*kMember{index}"
        if field["type"] == "float32":
            expression = f"static_cast<float64>({expression})"
        source += f"        {{ .Type = Kind::{enum}, .{member} = {expression} }},\n"
    source += f'''    }};
    const auto status = ValidateValues(kSchema, values, error);
    if (status != Status::Ok) {{ return status; }}
    if (output.size() < {count}) {{ error = {{ .Code = Status::BufferTooSmall }}; return error.Code; }}
    for (usize index = 0; index < {count}; ++index) {{ output[index] = values[index]; }}
    return Status::Ok;
}}
Status Prepare{name}(const {name}& source, std::span<const Edit> edits, {name}& output, Diagnostic& error) noexcept
{{
    Value current[{count}]{{}};
    auto status = Read{name}(source, current, error);
    if (status != Status::Ok) {{ return status; }}
    Value values[{count}]{{}};
    status = PrepareValues(kSchema, current, edits, values, error);
    if (status != Status::Ok) {{ return status; }}
    {name} candidate = source;
'''
    for index, field in enumerate(fields):
        source += f"    candidate.*kMember{index} = static_cast<{field['type']}>(values[{index}].{TYPES[field['type']][1]});\n"
    source += f'''    output = candidate;
    return Status::Ok;
}}
Status Read{name}Json(std::string_view input, {name}& output, const AllocationDomain& domain, Diagnostic& error) noexcept
{{
    Value values[{count}]{{}};
    const auto status = serialization::ReadJson(kSchema, input, values, domain, error);
    if (status != Status::Ok) {{ return status; }}
    {name} candidate = output;
'''
    for index, field in enumerate(fields):
        if field['persist']:
            source += f"    candidate.{field['member']} = static_cast<{field['type']}>(values[{index}].{TYPES[field['type']][1]});\n"
    source += f'''    Value verified[{count}]{{}};
    if (const auto validation = Read{name}(candidate, verified, error); validation != Status::Ok)
    {{
        return validation;
    }}
    output = candidate;
    return Status::Ok;
}}
Status Write{name}Json(const {name}& source, std::span<uint8> storage, std::span<const uint8>& output, Diagnostic& error) noexcept
{{
    Value values[{count}]{{}};
    const auto status = Read{name}(source, values, error);
    if (status != Status::Ok) {{ return status; }}
    return serialization::WriteJson(kSchema, values, storage, output, error);
}}
}} // namespace {namespace}
'''
    # Keep persistence in a separate archive object: a reflection-only gameplay
    # module need not pull the parser/allocator into its binary.
    split = source.index(f"Status Read{name}Json(")
    json_source = preamble + f'#include "{header_name}"\nnamespace {namespace}\n{{\nusing namespace ludus::foundation;\nusing namespace ludus::foundation::reflection;\n' + source[split:]
    json_source = json_source.replace('serialization::ReadJson(kSchema,', f'serialization::ReadJson({name}Schema(),')
    json_source = json_source.replace('serialization::WriteJson(kSchema,', f'serialization::WriteJson({name}Schema(),')
    core_source = source[:split] + f'}} // namespace {namespace}\n'
    return header, core_source, json_source


def write_changed(path, text):
    content = text.encode("utf-8")
    if path.exists() and path.read_bytes() == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(content)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--json-source", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    try:
        data = load(args.schema)
        if args.baseline:
            check_baseline(data, load(args.baseline))
        header, source, json_source = generate(data, args.header.name, args.schema.name)
        for path, text in ((args.header, header), (args.source, source), (args.json_source, json_source),
                           (args.manifest, json.dumps(data, ensure_ascii=True, sort_keys=True, indent=2) + "\n")):
            write_changed(path, text)
    except (ValueError, OSError, OverflowError, TypeError, RecursionError) as error:
        print(f"{args.schema}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
