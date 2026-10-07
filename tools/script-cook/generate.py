#!/usr/bin/env python3
"""Generate the closed S1 value/state/event and one-command contract.

Thanks to Julien Hamaide, "Automatic Lua Binding System", Game Programming
Gems 7, §7.1, pp. 503–516: generate inspectable ordinary bindings. We read an
explicit manifest, never arbitrary C++ or code snippets, and use copied
identities/POD trampolines instead of GC-owned objects. See the scripting Gems
review and docs/architecture/luau-s1.md for this deliberately narrow schema.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def require(ok, message):
    if not ok:
        raise ValueError(message)


def closed(obj, keys):
    require(type(obj) is dict and set(obj) == set(keys), "unknown or missing keys")


def unique(pairs):
    data = {}
    for key, value in pairs:
        require(key not in data, "duplicate JSON key")
        data[key] = value
    return data


def name(value):
    require(type(value) is str and len(value) <= 64 and re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", value),
            "invalid identifier")


def integer(value, low=1, high=(1 << 32) - 1):
    require(type(value) is int and low <= value <= high, "invalid integer")


def field(data, persisted=True):
    require(type(data) is dict, "field must be an object")
    name(data.get("name"))
    kind = data.get("type")
    require(kind in ("uint32", "bool", "EntityRef"), "unsupported field type")
    keys = ["name", "type"] + (["id"] if persisted else [])
    if kind == "uint32":
        keys += ["min", "max"] + (["default"] if persisted else [])
        integer(data["min"], 0)
        integer(data["max"], data["min"])
        if persisted:
            integer(data["default"], data["min"], data["max"])
    elif kind == "bool":
        require(persisted, "boolean command argument unsupported")
        keys += ["default"]
        require(type(data["default"]) is bool, "invalid boolean default")
    closed(data, keys)
    if persisted:
        integer(data["id"])


def load(path):
    require(path.stat().st_size <= 65536, "manifest exceeds 64 KiB")
    data = json.loads(path.read_text(), object_pairs_hook=unique)
    closed(data, ["version", "namespace", "entrypoint", "schemas", "operation"])
    require(type(data["version"]) is int and data["version"] == 1, "unsupported version")
    require(type(data["namespace"]) is str and len(data["namespace"]) <= 128, "invalid namespace")
    for part in data["namespace"].split("::"):
        name(part)
    name(data["entrypoint"])
    require(len(data["entrypoint"]) < 32, "entrypoint exceeds diagnostic capacity")
    closed(data["schemas"], ["Config", "State", "Interact"])
    ids = set()
    for role, record in data["schemas"].items():
        closed(record, ["id", "fields"])
        integer(record["id"])
        require(record["id"] not in ids, "duplicate schema ID")
        ids.add(record["id"])
        require(type(record["fields"]) is list and 0 < len(record["fields"]) <= 16, "invalid field count")
        fields, names = set(), set()
        for item in record["fields"]:
            field(item)
            require(item["id"] not in fields and item["name"] not in names, "duplicate field ID/name")
            require(item["type"] != "EntityRef" or role == "Interact", "references only supported in events")
            fields.add(item["id"])
            names.add(item["name"])
    op = data["operation"]
    closed(op, ["id", "name", "phase", "capability", "effect", "arguments", "result"])
    integer(op["id"])
    require(op["id"] not in ids, "duplicate operation ID")
    name(op["name"])
    require((op["phase"], op["capability"], op["effect"], op["result"]) ==
            ("Gameplay", "DoorControl", "Command", "CommandResult"), "unsupported operation contract")
    require(type(op["arguments"]) is list and len(op["arguments"]) == 2, "expected entity and uint32 arguments")
    for item in op["arguments"]:
        field(item, False)
    require([a["type"] for a in op["arguments"]] == ["EntityRef", "uint32"], "unsupported signature")
    require(op["arguments"][0]["name"] != op["arguments"][1]["name"], "duplicate argument name")
    return data


def generate(data, digest):
    ns = data["namespace"]
    h = """#pragma once
#include <ludus/foundation/base/core.h>
namespace NAMESPACE {
using namespace foundation;
struct EntityRef {
    uint64 World = 0; uint64 Session = 0; uint64 Execution = 0;
    uint32 Slot = 0; uint32 Generation = 0;
    [[nodiscard]] constexpr bool operator==(const EntityRef&) const noexcept = default;
};
""".replace("NAMESPACE", ns)
    definitions = "--!strict\n-- Generated authoring types; runtime userdata tags enforce opacity.\n"
    definitions += 'type EntityRef = { __opaque: "EntityRef" }\n'
    validators = ""
    for role, record in data["schemas"].items():
        h += f"inline constexpr uint32 {role.upper()}_SCHEMA_ID = {record['id']};\nstruct {role} {{\n"
        definitions += f"type {role} = {{\n"
        conditions = []
        for f in record["fields"]:
            kind, key = f["type"], f["name"]
            default = "{}" if kind == "EntityRef" else str(f["default"]).lower()
            h += f"    {kind} {key} = {default};\n"
            definitions += f"    {key}: " + {"uint32": "number", "bool": "boolean", "EntityRef": "EntityRef"}[kind] + ",\n"
            if kind == "uint32":
                if f["min"] > 0:
                    conditions.append(f"value.{key} >= {f['min']}")
                if f["max"] < (1 << 32) - 1:
                    conditions.append(f"value.{key} <= {f['max']}")
        h += "};\n"
        for f in record["fields"]:
            h += f"inline constexpr uint32 {role.upper()}_{f['name'].upper()}_FIELD_ID = {f['id']};\n"
            if f["type"] == "uint32":
                h += f"inline constexpr uint32 {role.upper()}_{f['name'].upper()}_MAX = {f['max']};\n"
        definitions += "}\n"
        validators += f"[[nodiscard]] inline bool Validate(const {role}& value) noexcept {{\n"
        validators += "    " + ("return " + " && ".join(conditions) + ";" if conditions else "(void)value; return true;") + "\n}\n"
    h += validators
    op = data["operation"]
    h += f"inline constexpr uint32 OPERATION_ID = {op['id']};\n"
    h += f'inline constexpr char ENTRYPOINT[] = "{data["entrypoint"]}";\n'
    h += f'inline constexpr const char* MANIFEST_SHA256 = "{digest}";\n'
    h += """struct Transaction;
struct CommandResult;
"""
    h += f"[[nodiscard]] CommandResult {op['name']}(Transaction&, EntityRef, uint32) noexcept;\n"
    h += f"inline constexpr uint32 POWER_MIN = {op['arguments'][1]['min']};\n"
    h += f"inline constexpr uint32 POWER_MAX = {op['arguments'][1]['max']};\n"
    h += "}\n"
    definitions += "type CommandResult = { Status: string, Token: number }\n"
    definitions += f"type Api = {{ {op['name']}: (EntityRef, number) -> CommandResult }}\n"

    cpp = """// Generated POD-only Luau adapter. Native services finish before VM pushes.
// Thanks to Celes, de Figueiredo and Ierusalimschy, "Binding C/C++ Objects to Lua",
// Game Programming Gems 6, §4.2, pp. 341–355: validate at the boundary and separate
// wrapper identity from host ownership. See docs/architecture/scripting-gems-review.md.
#include <ludus/foundation/base/core.h>
#include <ludus/foundation/math/scalar.hpp>
#include <cstring>
#include <new>
#include <type_traits>
#include <lua.h>
#include <lualib.h>
#include "internal/runtime.h"
#include "shared.h"
namespace NAMESPACE {
using namespace runtime::scripting;
namespace {
constexpr int32 ENTITY_TAG = 1;
constexpr int32 STATE_TAG = 2;
struct StateRef { uint64 Epoch; uint64 Instance; };
struct Range { uint32 Min; uint32 Max; };
static_assert(std::is_trivially_destructible_v<EntityRef>);
static_assert(std::is_trivially_destructible_v<StateRef>);
Transaction& Current(lua_State* state) noexcept {
    CallContext& call = GetCallContext(state);
    if (!call.Active || call.User == nullptr) { Reject(state, 1, "expired invocation"); }
    return *static_cast<Transaction*>(call.User);
}
uint32 Number(lua_State* state, int32 index, Range range) noexcept {
    if (lua_type(state, index) != LUA_TNUMBER) { Reject(state, 2, "exact number required"); }
    const float64 number = lua_tonumber(state, index);
    // NaN/Inf and out-of-range values fail before the narrowing conversion.
    if (!foundation::math::IsFinite(number) || number < range.Min || number > range.Max)
    { Reject(state, 2, "number outside declared range"); }
    const uint32 value = static_cast<uint32>(number);
    if (static_cast<float64>(value) != number) { Reject(state, 2, "integer required"); }
    return value;
}
EntityRef Entity(lua_State* state, int32 index) noexcept {
    const auto* entity = static_cast<const EntityRef*>(lua_touserdatatagged(state, index, ENTITY_TAG));
    if (entity == nullptr) { Reject(state, 3, "checked entity reference required"); }
    return *entity;
}
void PushEntity(lua_State* state, EntityRef value) noexcept {
    auto* output = static_cast<EntityRef*>(lua_newuserdatataggedwithmetatable(state, sizeof(EntityRef), ENTITY_TAG));
    new (output) EntityRef(value);
}
int32 Equal(lua_State* state) noexcept {
    lua_pushboolean(state, Entity(state, 1) == Entity(state, 2));
    return 1;
}
Transaction& Candidate(lua_State* state) noexcept {
    const auto* ref = static_cast<const StateRef*>(lua_touserdatatagged(state, 1, STATE_TAG));
    Transaction& txn = Current(state);
    if (ref == nullptr || ref->Epoch != GetCallContext(state).Epoch || ref->Instance != txn.Instance)
    { Reject(state, 4, "expired state facade"); }
    return txn;
}
const char* Key(lua_State* state) noexcept {
    if (lua_type(state, 2) != LUA_TSTRING) { Reject(state, 5, "state field name required"); }
    usize length = 0;
    const char* key = lua_tolstring(state, 2, &length);
    if (length != std::strlen(key)) { Reject(state, 5, "invalid state field name"); }
    return key;
}
int32 ReadState(lua_State* state) noexcept {
    const State& value = Candidate(state).CandidateState;
    const char* key = Key(state);
READ_FIELDS
    Reject(state, 5, "unknown state field");
    return 0;
}
int32 WriteState(lua_State* state) noexcept {
    State& value = Candidate(state).CandidateState;
    const char* key = Key(state);
WRITE_FIELDS
    Reject(state, 5, "unknown state field");
    return 0;
}
int32 InvokeCommand(lua_State* state) noexcept {
    GetCallContext(state).Operation = OPERATION_ID;
    Transaction& txn = Current(state);
    if (lua_gettop(state) != 2) { Reject(state, 6, "command expects entity and power"); }
    const EntityRef target = Entity(state, 1);
    const uint32 power = Number(state, 2, { .Min = POWER_MIN, .Max = POWER_MAX });
    const CommandResult result = COMMAND_NAME(txn, target, power);
    lua_createtable(state, 0, 2);
    lua_pushstring(state, ResultName(result.Code)); lua_setfield(state, -2, "Status");
    lua_pushnumber(state, result.Token); lua_setfield(state, -2, "Token");
    lua_setreadonly(state, -1, 1);
    return 1;
}
void Locked(lua_State* state) noexcept {
    lua_pushstring(state, "locked"); lua_setfield(state, -2, "__metatable");
    lua_setreadonly(state, -1, 1);
}
}
int32 Install(lua_State* state) noexcept {
    lua_createtable(state, 0, 2);
    lua_pushcfunction(state, Equal, "EntityRef.__eq"); lua_setfield(state, -2, "__eq");
    Locked(state); lua_setuserdatametatable(state, ENTITY_TAG);
    lua_createtable(state, 0, 3);
    lua_pushcfunction(state, ReadState, "State.__index"); lua_setfield(state, -2, "__index");
    lua_pushcfunction(state, WriteState, "State.__newindex"); lua_setfield(state, -2, "__newindex");
    Locked(state); lua_setuserdatametatable(state, STATE_TAG);
    return 0;
}
int32 PushArguments(lua_State* state) noexcept {
    const Transaction& txn = Current(state);
    lua_createtable(state, 0, CONFIG_COUNT);
CONFIG_FIELDS
    lua_setreadonly(state, -1, 1);
    auto* facade = static_cast<StateRef*>(lua_newuserdatataggedwithmetatable(state, sizeof(StateRef), STATE_TAG));
    new (facade) StateRef{GetCallContext(state).Epoch, txn.Instance};
    lua_createtable(state, 0, EVENT_COUNT);
EVENT_FIELDS
    lua_setreadonly(state, -1, 1);
    lua_createtable(state, 0, 1);
    lua_pushcfunction(state, InvokeCommand, "COMMAND_NAME"); lua_setfield(state, -2, "COMMAND_NAME");
    lua_setreadonly(state, -1, 1);
    return 4;
}
}
""".replace("NAMESPACE", ns).replace("COMMAND_NAME", op["name"])
    reads, writes = [], []
    for f in data["schemas"]["State"]["fields"]:
        key, kind = f["name"], f["type"]
        push = "lua_pushboolean" if kind == "bool" else "lua_pushnumber"
        reads.append(f'    if (std::strcmp(key, "{key}") == 0) {{ {push}(state, value.{key}); return 1; }}')
        if kind == "bool":
            write = f'if (lua_type(state, 3) != LUA_TBOOLEAN) {{ Reject(state, 2, "boolean required"); }} value.{key} = lua_toboolean(state, 3) != 0;'
        else:
            write = f'value.{key} = Number(state, 3, {{ .Min = {f["min"]}, .Max = {f["max"]} }});'
        writes.append(f'    if (std::strcmp(key, "{key}") == 0) {{ {write} return 0; }}')
    cpp = cpp.replace("READ_FIELDS", "\n".join(reads)).replace("WRITE_FIELDS", "\n".join(writes))
    for role, prefix in (("Config", "CONFIG"), ("Interact", "EVENT")):
        lines = []
        for f in data["schemas"][role]["fields"]:
            expr = f"txn.{'Configuration' if role == 'Config' else 'Event'}.{f['name']}"
            if f["type"] == "EntityRef":
                lines.append(f"    PushEntity(state, {expr});")
            else:
                push = "lua_pushboolean" if f["type"] == "bool" else "lua_pushnumber"
                lines.append(f"    {push}(state, {expr});")
            lines.append(f'    lua_setfield(state, -2, "{f["name"]}");')
        cpp = cpp.replace(prefix + "_COUNT", str(len(data["schemas"][role]["fields"])))
        cpp = cpp.replace(prefix + "_FIELDS", "\n".join(lines))
    return {"contract.h": h, "contract.d.luau": definitions, "bindings.cpp": cpp,
            "contract.json": json.dumps(data, indent=2) + "\n"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = load(args.manifest)
    digest = hashlib.sha256(args.manifest.read_bytes()).hexdigest()
    args.output.mkdir(parents=True, exist_ok=True)
    for key, text in generate(data, digest).items():
        (args.output / key).write_text(text)


if __name__ == "__main__":
    main()
