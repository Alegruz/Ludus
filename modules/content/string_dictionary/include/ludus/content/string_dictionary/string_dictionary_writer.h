#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/containers/borrowed_view.hpp>
#include <ludus/foundation/memory/allocation_domain.hpp>
#include <ludus/foundation/strings/string_table.hpp>

#include <ludus/content/string_dictionary/string_dictionary.h>
#include <ludus/content/string_dictionary_format/lustr.h>

#include <string_view>

/// @file
/// Deterministic LUSTR dictionary writer for cookers (Ludus::StringDictionaryWriter).
///
/// Runtime code reads dictionaries with Ludus::StringDictionary and never needs
/// to link this writer. See docs/architecture/strings.md, "Cooked dictionaries
/// and persistence".
namespace ludus::content::string_dictionary
{
/// Cold setup limits for one dictionary writer.
struct StringDictionaryWriterConfig final
{
    /// BYTE_POLICY_UTF8 rejects malformed UTF-8; both policies reject zero bytes.
    uint32 BytePolicy{string_dictionary_format::BYTE_POLICY_UTF8};
    /// Most unique spellings, including the empty entry; 1..MAX_ENTRIES.
    uint32 MaxEntries{string_dictionary_format::MAX_ENTRIES};
    /// Longest spelling in bytes; at most MAX_SPELLING_BYTES.
    usize MaxSpellingBytes{string_dictionary_format::MAX_SPELLING_BYTES};
    /// Largest spelling blob including terminators; 1..MAX_BLOB_BYTES.
    usize MaxBlobBytes{string_dictionary_format::MAX_BLOB_BYTES};
    /// Budget for the deduplication table, including its growth peak. TryFinish
    /// additionally allocates working storage proportional to the entry count.
    usize MaxTableBytes{usize{64} * 1024 * 1024};
};

/// @brief Collects spellings, deduplicates them, and encodes one canonical dictionary.
///
/// Lifecycle: create, TryAdd spellings in any order, TryFinish, then TryRemap
/// record references and TryEncode. The encoded bytes depend only on the set of
/// unique spellings, the byte policy and the revision, never on arrival order,
/// worker scheduling or hash seeds.
///
/// TryAdd returns provisional indices in first-arrival order (the empty
/// spelling is always 0). TryFinish sorts unique spellings by unsigned byte
/// order; callers then rewrite every stored provisional index through TryRemap.
///
/// Move-only and not internally synchronized: callers serialize all calls on one
/// writer. The allocation domain must outlive the writer. Spellings are copied,
/// so callers may release their input immediately after TryAdd returns.
class StringDictionaryWriter final
{
public:
    /// Constructs an empty writer; create one with CreateStringDictionaryWriter.
    StringDictionaryWriter() noexcept = default;
    /// Releases all working storage through the original domain.
    ~StringDictionaryWriter() noexcept;
    /// Transfers the writer and leaves other empty.
    StringDictionaryWriter(StringDictionaryWriter&& other) noexcept;
    /// Releases this writer, then takes other's state and leaves other empty.
    StringDictionaryWriter& operator=(StringDictionaryWriter&& other) noexcept;
    /// Writers own unique working storage and cannot be copied.
    StringDictionaryWriter(const StringDictionaryWriter&) = delete;
    /// Writers own unique working storage and cannot be copy-assigned.
    StringDictionaryWriter& operator=(const StringDictionaryWriter&) = delete;

    /// Whether the writer was created and has not been moved from.
    [[nodiscard]] bool IsValid() const noexcept;
    /// Whether TryFinish has succeeded.
    [[nodiscard]] bool IsFinished() const noexcept;
    /// Number of unique spellings including the empty entry; zero when empty.
    [[nodiscard]] uint32 GetEntryCount() const noexcept;

    /// @brief Adds one spelling, or finds its existing entry.
    ///
    /// A duplicate succeeds without allocating, even at the configured limits.
    /// @param spelling Counted bytes, validated against the byte policy and limits.
    /// @param outProvisionalIndex Receives the arrival-order index. Unchanged on failure.
    /// @return Ok, InvalidHandle, InvalidState after TryFinish, LimitExceeded,
    /// InvalidUtf8, EmbeddedZero or OutOfMemory. Failure leaves the writer unchanged.
    [[nodiscard]] StringDictionaryStatus TryAdd(std::string_view spelling,
                                                foundation::StringIndex& outProvisionalIndex) noexcept;
    /// @brief Sorts the unique spellings and fixes the final directory indices.
    ///
    /// @return Ok, InvalidHandle, InvalidState if already finished, or OutOfMemory.
    /// On failure the writer remains unfinished and usable.
    [[nodiscard]] StringDictionaryStatus TryFinish() noexcept;
    /// @brief Maps a provisional index from TryAdd to its final directory index.
    ///
    /// @param provisionalIndex Index returned by this writer's TryAdd.
    /// @param outIndex Receives the final index. Unchanged on failure.
    /// @return Ok, InvalidState before TryFinish, or InvalidHandle for an empty
    /// writer or an index this writer never returned.
    [[nodiscard]] StringDictionaryStatus TryRemap(foundation::StringIndex provisionalIndex,
                                                  foundation::StringIndex& outIndex) const noexcept;
    /// Exact encoded size in bytes after TryFinish; zero before it.
    [[nodiscard]] usize GetEncodedSize() const noexcept;
    /// @brief Encodes the finished dictionary into caller storage, without allocating.
    ///
    /// @param revision Content revision recorded in the header and checked by readers.
    /// @param destination Writable storage of exactly GetEncodedSize() bytes; any
    /// alignment. Left untouched on failure.
    /// @return Ok, InvalidHandle, InvalidState before TryFinish, or
    /// InvalidArgument for an invalid view or a size mismatch.
    [[nodiscard]] StringDictionaryStatus TryEncode(const RevisionDigest& revision,
                                                   foundation::BorrowedView<uint8> destination) const noexcept;

private:
    struct State;
    State* mStateOrNull{};

    friend StringDictionaryStatus CreateStringDictionaryWriter(const StringDictionaryWriterConfig& config,
                                                               const foundation::AllocationDomain& domain,
                                                               StringDictionaryWriter& outWriter) noexcept;
};

/// @brief Creates an empty writer holding only the empty entry.
///
/// @param config Limits and byte policy; validated against the format limits.
/// @param domain Allocation domain for all working storage; must outlive the writer.
/// @param outWriter Must be empty. Receives the writer on success; unchanged on failure.
/// @return Ok, InvalidArgument, OutOfMemory or LimitExceeded.
[[nodiscard]] StringDictionaryStatus CreateStringDictionaryWriter(const StringDictionaryWriterConfig& config,
                                                                  const foundation::AllocationDomain& domain,
                                                                  StringDictionaryWriter& outWriter) noexcept;
} // namespace ludus::content::string_dictionary
