#pragma once

// Live property schema and edit records (design section 9).
//
// Properties are described with stable object/property IDs, never runtime
// addresses. The schema is copied over IPC into the editor; no widget stores a
// pointer or member offset into a game instance. These are the wire records
// shared by the module ABI (DescribeProperties/ReadProperties/PrepareEdits) and
// the editor inspector. They are POD with explicit fixed-width fields; the host
// and module serialize them into the bounded ByteSpan/ByteView buffers.

#include <ludus/foundation/base/types.h>

namespace ludus::runtime::game_api
{
using ludus::foundation::float32;
using ludus::foundation::int32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;

// Bounds mirrored from design section 10 (protocol budgets).
inline constexpr uint32 kMaxObjects = 256;
inline constexpr uint32 kMaxProperties = 4096;
inline constexpr uint32 kMaxStringBytes = 4096;
inline constexpr uint32 kMaxEditBatch = 64;
inline constexpr uint32 kMaxLabelBytes = 64;

// Supported property value kinds for this phase: bool, int32, finite float32,
// enum and bounded UTF-8 string (design 9).
enum class PropertyKind : uint32
{
    Bool = 0,
    Int32 = 1,
    Float32 = 2,
    Enum = 3,
    String = 4
};

// Persistence scope: whether a value is a persistable authored parameter or a
// read-only simulation field. Only persistable fields reach Apply to Document.
enum class PropertyScope : uint32
{
    SessionOnly = 0,  // Live-editable, not persistable.
    Persistable = 1,  // Authored parameter, eligible for Apply to Document.
    ReadOnly = 2      // Simulation output; never pretends to be editable.
};

// One property descriptor. Values/bounds are typed; the host validates against
// kind and range at receipt and again at the safe boundary (design 10).
struct PropertyDescriptor final
{
    uint64 ObjectId = 0;    // Stable object identity, preserved across reload.
    uint64 PropertyId = 0;  // Stable property identity within the object.
    uint32 Kind = 0;        // PropertyKind.
    uint32 Scope = 0;       // PropertyScope.
    uint8 Writable = 0;     // 1 if the module accepts edits to this property.
    uint8 LabelLength = 0;  // Bytes of Label used (<= kMaxLabelBytes).
    uint8 Reserved0 = 0;
    uint8 Reserved1 = 0;

    // Inclusive bounds for numeric kinds; enum uses [0, MaxInt] over variants.
    int32 MinInt = 0;
    int32 MaxInt = 0;
    float32 MinFloat = 0;
    float32 MaxFloat = 0;

    char Label[kMaxLabelBytes] = {};
};

// A property value snapshot returned by ReadProperties. Carries the object
// revision so the editor can detect simulation writes invalidating a stale edit.
struct PropertyValue final
{
    uint64 ObjectId = 0;
    uint64 PropertyId = 0;
    uint64 ObjectRevision = 0;
    uint32 Kind = 0;        // PropertyKind.
    uint32 IntOrEnum = 0;   // Bool (0/1), Int32 (reinterpret), or enum ordinal.
    float32 Float = 0;
    uint32 StringLength = 0; // Bytes of String used for String kind.
    char String[kMaxStringBytes / 16] = {}; // Bounded inline string (256 bytes).
};

// A single typed edit within an all-or-none batch (design 9/10).
struct PropertyEdit final
{
    uint64 ObjectId = 0;
    uint64 PropertyId = 0;
    uint64 ExpectedRevision = 0; // Conflict if the live revision differs.
    uint32 Kind = 0;             // PropertyKind, must match the descriptor.
    uint32 IntOrEnum = 0;
    float32 Float = 0;
    uint32 StringLength = 0;
    char String[kMaxStringBytes / 16] = {};
};

// Header for a serialized edit batch carried in PrepareEdits' ByteView.
struct EditBatchHeader final
{
    uint32 StructSize = 0;
    uint32 Count = 0;        // Number of PropertyEdit entries (<= kMaxEditBatch).
    uint32 SchemaVersion = 0;
    uint32 CommandId = 0;    // For duplicate/lost reply reconciliation.
};
} // namespace ludus::runtime::game_api
