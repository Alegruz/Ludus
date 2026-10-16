#pragma once

#include <ludus/foundation/base/compiler.h>
#include <ludus/foundation/base/types.h>

#include <ludus/foundation/containers/borrowed_view.hpp>
#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/strings/string_table.hpp>

#include <ludus/content/string_dictionary_format/lustr.h>

#include <string_view>

/// @file
/// Validating reader for LUSTR cooked string dictionaries (Ludus::StringDictionary).
///
/// See docs/architecture/strings.md, "Cooked dictionaries and persistence", and
/// ludus/content/string_dictionary_format/lustr.h for the wire contract.
namespace ludus::content::string_dictionary
{
using foundation::uint32;
using foundation::uint8;
using foundation::usize;

/// Result of a dictionary read, lookup or write operation.
enum class StringDictionaryStatus : uint8
{
    Ok,                 ///< The operation succeeded.
    InvalidArgument,    ///< A configuration, view or output argument violates the API contract.
    Malformed,          ///< The bytes violate the structural or canonical-order rules of the format.
    UnsupportedVersion, ///< The header names a format version this reader does not implement.
    RevisionMismatch,   ///< The dictionary belongs to a different content revision than expected.
    LimitExceeded,      ///< A count, spelling length or byte size exceeds a format or configured limit.
    InvalidUtf8,        ///< A spelling is not strict UTF-8 under BYTE_POLICY_UTF8.
    EmbeddedZero,       ///< A spelling contains a zero byte.
    OutOfMemory,        ///< The allocation domain could not supply working storage.
    InvalidHandle,      ///< The dictionary or writer is empty, or an index is out of range.
    NotFound,           ///< No entry has exactly the requested spelling.
    InvalidState,       ///< The writer is not in the lifecycle state that the operation requires.
};

/// Failure detail without owned text; Offset is a byte offset into the dictionary.
struct StringDictionaryDiagnostic final
{
    StringDictionaryStatus Status{StringDictionaryStatus::Ok}; ///< Same value as the returned status.
    usize Offset{};                                            ///< First offending byte; zero on success.
};

/// Producer-supplied identity of the content revision a dictionary belongs to.
using RevisionDigest = foundation::StaticArray<uint8, string_dictionary_format::REVISION_DIGEST_BYTES>;

/// @brief Validated, read-only view of a LUSTR dictionary held in caller-owned bytes.
///
/// The view copies no spelling bytes. Whoever owns the dictionary bytes (for
/// example a content snapshot or a read-only mapping) must keep them alive and
/// unmodified for as long as the view, or any spelling returned from it, is used.
/// Views are trivially copyable and need no synchronization for concurrent reads.
/// A default-constructed view is empty and every lookup reports InvalidHandle.
class StringDictionaryView final
{
public:
    /// Constructs an empty view.
    constexpr StringDictionaryView() noexcept = default;

    /// Whether the view holds a validated dictionary.
    [[nodiscard]] LUDUS_INLINE bool IsValid() const noexcept
    {
        return mEntryCount != 0;
    }
    /// Number of entries including the empty entry at index zero; zero when empty.
    [[nodiscard]] LUDUS_INLINE uint32 GetEntryCount() const noexcept
    {
        return mEntryCount;
    }
    /// BYTE_POLICY_UTF8 or BYTE_POLICY_BYTES; zero when empty.
    [[nodiscard]] LUDUS_INLINE uint32 GetBytePolicy() const noexcept
    {
        return mBytePolicy;
    }
    /// @brief Copies the dictionary's revision digest.
    ///
    /// @return The header digest, or all zero bytes for an empty view.
    [[nodiscard]] RevisionDigest GetRevision() const noexcept;
    /// @brief Resolves a directory index in constant time without allocating.
    ///
    /// @param index Index from this dictionary; indices are not portable across revisions.
    /// @param outSpelling Receives the counted spelling, borrowed from the dictionary
    /// bytes. The byte after it is a zero terminator. Unchanged on failure.
    /// @return Ok, or InvalidHandle for an empty view or an out-of-range index.
    [[nodiscard]] StringDictionaryStatus TryResolve(foundation::StringIndex index,
                                                    std::string_view& outSpelling) const noexcept;
    /// @brief Finds the entry with exactly these bytes by binary search.
    ///
    /// Comparison is exact unsigned byte order; nothing is normalized or case folded.
    /// @param spelling Counted bytes to look up; the empty spelling finds index zero.
    /// @param outIndex Receives the matching index. Unchanged on failure.
    /// @return Ok, NotFound, or InvalidHandle for an empty view.
    [[nodiscard]] StringDictionaryStatus TryFind(std::string_view spelling,
                                                 foundation::StringIndex& outIndex) const noexcept;

private:
    const uint8* mBytesOrNull{};
    uint32 mEntryCount{};
    uint32 mBytePolicy{};

    [[nodiscard]] std::string_view getSpelling(uint32 index) const noexcept;

    friend StringDictionaryStatus ReadStringDictionary(foundation::BorrowedView<const uint8> bytes,
                                                       const RevisionDigest& expectedRevision,
                                                       StringDictionaryView& outDictionary,
                                                       StringDictionaryDiagnostic& outDiagnostic) noexcept;
};

/// @brief Validates a LUSTR dictionary and binds a view to the caller's bytes.
///
/// Every header field, size, offset, terminator, byte policy and the canonical
/// strictly increasing order are checked before any spelling becomes visible.
/// Unaligned input is supported. No allocation or I/O occurs, and nothing is copied.
/// @param bytes The complete dictionary; trailing bytes are rejected. The caller
/// keeps them alive and unmodified while the resulting view is used.
/// @param expectedRevision Revision the caller's records were cooked against; a
/// dictionary from any other revision is rejected rather than remapped.
/// @param outDictionary Receives the view on success; unchanged on failure.
/// @param outDiagnostic Receives the status and, on failure, the first offending byte offset.
/// @return Ok, InvalidArgument, Malformed, UnsupportedVersion, RevisionMismatch,
/// LimitExceeded, InvalidUtf8 or EmbeddedZero.
[[nodiscard]] StringDictionaryStatus ReadStringDictionary(foundation::BorrowedView<const uint8> bytes,
                                                          const RevisionDigest& expectedRevision,
                                                          StringDictionaryView& outDictionary,
                                                          StringDictionaryDiagnostic& outDiagnostic) noexcept;
} // namespace ludus::content::string_dictionary
