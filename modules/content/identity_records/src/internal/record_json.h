#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/content/content.h>
#include <ludus/content/identity_records/migration_manifest.h>
#include <ludus/foundation/identity/uuid.hpp>
#include <ludus/foundation/parsing/json.hpp>

#include <string_view>

namespace ludus::content::identity_records::internal
{
/// Record a failure in `outError` and return its status.
IdentityRecordStatus Fail(IdentityRecordError& outError,
                          IdentityRecordStatus status,
                          std::string_view what,
                          std::string_view subject = {}) noexcept;

/// Parse one canonical-or-uppercase UUID string that must be a nonnil version-4 identity.
[[nodiscard]] bool TryReadIdentity(foundation::parsing::JsonValue value, foundation::identity::Uuid& outValue) noexcept;

/// @brief Canonical indented JSON over a fixed-capacity buffer.
///
/// Callers own member order. Two-space indentation, `"key": value`, one member or
/// element per line. Overflow surfaces from Finish() as Limit; no prefix escapes.
class IndentedWriter final
{
public:
    /// Allocate `capacity` bytes of scratch; IsReady() reports allocation failure.
    explicit IndentedWriter(usize capacity) noexcept;
    IndentedWriter(const IndentedWriter&) = delete;
    IndentedWriter& operator=(const IndentedWriter&) = delete;

    /// Report whether the scratch buffer was allocated.
    [[nodiscard]] LUDUS_INLINE bool IsReady() const noexcept
    {
        return mIsReady;
    }
    /// Open an object or array (`{`/`[`) and indent its contents.
    void Open(char bracket) noexcept;
    /// Close the innermost object or array with `bracket`.
    void Close(char bracket) noexcept;
    /// Start the next member: separator, newline, indentation, then `"key": `.
    void BeginMember(std::string_view key) noexcept;
    /// Start the next array element: separator, newline and indentation.
    void BeginElement() noexcept;
    /// Write a string scalar.
    void WriteString(std::string_view value) noexcept;
    /// Write a nonnegative integer scalar.
    void WriteInteger(uint64 value) noexcept;
    /// Write a UUID as canonical lowercase text.
    void WriteUuid(const foundation::identity::Uuid& value) noexcept;
    /// Copy a parsed JSON value, keeping object member order.
    void WriteValue(foundation::parsing::JsonValue value) noexcept;
    /// Finish with a trailing newline and copy the document into `outBytes`.
    [[nodiscard]] IdentityRecordStatus Finish(Bytes& outBytes) noexcept;

private:
    void beginItem() noexcept;

    Bytes mScratch;
    foundation::parsing::JsonWriter mWriter;
    usize mDepth = 0;
    bool mIsFirstItem = true;
    bool mIsReady = false;
    bool mHasFailed = false;
};

/// Parser limits shared by manifest and World readers.
[[nodiscard]] foundation::parsing::JsonLimits MakeLimits(usize maxInputBytes) noexcept;
} // namespace ludus::content::identity_records::internal
