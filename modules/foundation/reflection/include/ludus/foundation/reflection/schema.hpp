#pragma once

// Thanks to Frederic My, "Using Custom RTTI Properties to Stream and Edit
// Objects", Game Programming Gems 4, section 1.12, pp. 111-124: one immutable
// property description drives persistence and editing. These independently
// written tables use explicit IDs and typed generated access, without offsets
// or a reflective base class. See docs/architecture/reflection-serialization.md
// and reflection-serialization-gems-review.md for the sources and departures.
#include <ludus/foundation/base/types.h>

#include <span>
#include <string_view>

namespace ludus::foundation::reflection
{
inline constexpr usize MAX_FIELDS = 64;

enum class Kind : uint8
{
    Bool,
    Int32,
    Uint32,
    Int64,
    Uint64,
    Float32,
    Float64,
};

enum class Status : uint8
{
    Ok,
    InvalidSchema,
    InvalidValue,
    UnknownField,
    DuplicateField,
    ReadOnly,
    BufferTooSmall,
    InvalidArgument,
    InvalidDocument,
    SchemaChanged,
    OutOfMemory,
    LimitExceeded,
    UnsupportedKind,
};

struct SchemaId final
{
    uint8 Bytes[16]{}; // UUID bytes in canonical textual order, never native integers.
};

// Owned scalar snapshot. Only the member selected by Type is meaningful.
// Int64/Uint64 never pass through a floating-point representation.
struct Value final
{
    Kind Type = Kind::Bool;
    bool Boolean = false;
    int64 Signed = 0;
    uint64 Unsigned = 0;
    float64 Real = 0;
};

// Thanks to Google, Protocol Buffers "Language Guide (proto3)", "Assigning Field
// Numbers" / "Reserved Field Numbers": https://protobuf.dev/programming-guides/proto3/
// Stable numbers and reserved retired IDs inform our independent schema contract.
struct Field final
{
    uint32 Id = 0; // Nonzero, scoped to the schema; reserve retired IDs forever.
    std::string_view Key;
    std::string_view Label;
    Value Default;
    Value Min;
    Value Max;
    bool Persist = false;
    bool Editable = false;
};

struct Schema final
{
    SchemaId Id;
    uint32 Version = 0;
    std::string_view Name;
    std::span<const Field> Fields;
};

struct Edit final
{
    uint32 FieldId = 0;
    Value Data;
};

struct Diagnostic final
{
    Status Code = Status::Ok;
    uint32 FieldId = 0;
    usize ByteOffset = ~usize{0}; // Unknown offset is not a location in the file.
};

// Borrowed immutable tables must outlive every call. No registry, static
// registration, object pointers, native layout, or module ownership is hidden
// here. Generated getters are explicitly called by the owner's composition root.
[[nodiscard]] Status ValidateSchema(const Schema& schema, Diagnostic& error) noexcept;
[[nodiscard]] Status ValidateValue(const Field& field, const Value& value) noexcept;
[[nodiscard]] const Field* FindField(const Schema& schema, uint32 id) noexcept;
[[nodiscard]] Status ValidateValues(const Schema& schema, std::span<const Value> values, Diagnostic& error) noexcept;

// Field-order snapshots. The destination is unchanged on every failure;
// source and destination may alias. Bounded stack staging, no allocation.
[[nodiscard]] Status PrepareValues(const Schema& schema,
                                   std::span<const Value> current,
                                   std::span<const Edit> edits,
                                   std::span<Value> output,
                                   Diagnostic& error) noexcept;
} // namespace ludus::foundation::reflection
