#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/memory/allocation_domain.hpp>
#include <ludus/foundation/parsing/parsing.hpp>

#include <initializer_list>
#include <span>
#include <string_view>

namespace ludus::foundation::parsing
{
// Hard safety ceilings bound recursion and quadratic duplicate-key comparison.
// Callers may lower these, but cannot raise them in the current backend.
inline constexpr usize MAX_JSON_DEPTH = 64;
inline constexpr usize MAX_JSON_OBJECT_MEMBERS = 256;

struct JsonLimits final
{
    usize MaxInputBytes = usize{1024} * 1024;
    usize MaxWorkspaceBytes = usize{16} * 1024 * 1024;
    // Root has depth zero. Keys are not values; each container counts as a value.
    usize MaxDepth = 32;
    usize MaxObjectMembers = 32;
    usize MaxArrayElements = usize{1024} * 1024;
    usize MaxValues = usize{1024} * 1024;
    // Decoded UTF-8 byte length, applied to keys as well as string values.
    usize MaxStringBytes = usize{1024} * 1024;
};

// Borrowed nodes/strings survive only until their document's next Read/Reset or
// destruction. The legacy opaque Value member preserves Content source
// compatibility; never dereference it or construct nodes from foreign pointers.
struct JsonValue final
{
    const void* Value = nullptr;
    [[nodiscard]] bool Valid() const noexcept
    {
        return Value != nullptr;
    }
    [[nodiscard]] JsonValue Get(std::string_view key) const noexcept;
    [[nodiscard]] JsonValue Get(const char* key) const noexcept;
    [[nodiscard]] JsonValue At(usize index) const noexcept;
    [[nodiscard]] usize Count() const noexcept; // Array count, for Content compatibility.
    [[nodiscard]] bool Array() const noexcept;
    [[nodiscard]] bool Object() const noexcept;
    [[nodiscard]] bool Null() const noexcept;
    // Type mismatches leave output unchanged. Integer conversions are exact;
    // Number follows the pinned codec's integer-to-float64 conversion behavior.
    [[nodiscard]] bool String(std::string_view& output) const noexcept;
    [[nodiscard]] bool Integer(uint64& output) const noexcept;
    [[nodiscard]] bool SignedInteger(int64& output) const noexcept;
    [[nodiscard]] bool Number(float64& output) const noexcept;
    [[nodiscard]] bool Boolean(bool& output) const noexcept;
    [[nodiscard]] bool Fields(std::initializer_list<const char*> required) const noexcept;
};

// Strict, whole-document JSON, backed by pinned yyjson. Input is immutable and
// copied into a fallible, document-owned pool. One pool allocation, no per-node
// engine allocations. Reuse retains capacity; Release returns it to the domain.
// The domain must outlive this document. Different documents are independent;
// concurrent access to the same document requires external synchronization.
class JsonDocument final
{
public:
    JsonDocument() noexcept;
    explicit JsonDocument(const AllocationDomain& domain) noexcept;
    ~JsonDocument() noexcept;
    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;
    // Moves transfer the pool and domain identity. The source becomes empty and
    // reusable with its original domain; views follow the destination owner.
    JsonDocument(JsonDocument&& other) noexcept;
    JsonDocument& operator=(JsonDocument&& other) noexcept;

    // Every attempt invalidates old views. Input must not refer to this
    // document's old nodes/strings. Failure exposes no partial root.
    [[nodiscard]] ParseStatus Read(std::string_view input, ParseError& error, const JsonLimits& limits = {}) noexcept;
    [[nodiscard]] JsonValue Root() const noexcept;
    [[nodiscard]] usize WorkspaceBytes() const noexcept
    {
        return mCapacity;
    }
    void Reset() noexcept;
    void Release() noexcept;

private:
    const AllocationDomain* mDomain;
    void* mDocument = nullptr;
    void* mPool = nullptr;
    usize mCapacity = 0;
};

// Bounded primitives for domain-owned canonical writers, not a schema builder.
// Raw is trusted syntax; the caller owns delimiter/field order. String rejects
// invalid UTF-8, Number rejects NaN/Inf. A failed writer never exposes its prefix.
class JsonWriter final
{
public:
    explicit JsonWriter(std::span<uint8> output) noexcept : mOutput(output) {}
    void Raw(std::string_view value) noexcept;
    void String(std::string_view value) noexcept;
    void Integer(uint64 value) noexcept;
    void Number(float64 value) noexcept;
    void Boolean(bool value) noexcept;
    // Appends one newline. Repeated Finish is idempotent; subsequent writes
    // fail with InvalidState. On failure the caller's output view is unchanged.
    [[nodiscard]] ParseStatus Finish(std::span<const uint8>& output) noexcept;

private:
    std::span<uint8> mOutput;
    usize mSize = 0;
    ParseStatus mStatus = ParseStatus::Ok;
    bool mFinished = false;
};
} // namespace ludus::foundation::parsing
