#pragma once

#include <ludus/foundation/base/types.h>

#include <string_view>

/// @file
/// LUSTR cooked string dictionary format (Ludus::StringDictionaryFormat).
///
/// This header is the single contract between the dictionary writer used by
/// cookers (Ludus::StringDictionaryWriter) and the validating runtime reader
/// (Ludus::StringDictionary). It is owned by neither side and depends on nothing
/// but FoundationBase. See docs/architecture/strings.md, "Cooked dictionaries and
/// persistence", for the design contract.
///
/// A dictionary is little-endian and tightly packed, in this order:
/// a HEADER_BYTES header; EntryCount directory entries of DIRECTORY_ENTRY_BYTES
/// each; BlobBytes of spelling bytes. The offsets below are the exact wire layout,
/// derived from the field widths rather than from any host struct, so host
/// endianness and padding never leak into files.
///
/// Version 1 dictionaries are canonical, so equal spelling sets always encode to
/// equal bytes:
/// - Entry 0 is the empty spelling at blob offset 0.
/// - Spellings are unique and strictly increasing in unsigned byte order.
/// - Each spelling is followed by one zero byte. Entry i + 1 starts immediately
///   after entry i's terminator, and the last terminator ends the blob.
/// - Lengths exclude the terminator and are authoritative; spellings never
///   contain a zero byte. BYTE_POLICY_UTF8 spellings are also strict UTF-8.
///
/// Directory indices are meaningful only together with the dictionary that holds
/// them, identified by its revision digest. They are not process-local table IDs.
namespace ludus::content::string_dictionary_format
{
using foundation::core::uint32;
using foundation::core::uint8;
using foundation::core::usize;

/// Eight-byte file magic, including the trailing NUL.
inline constexpr std::string_view MAGIC{"LUSTR01\0", 8};
/// Version 1: canonical sorted spellings with a uint32 offset/length directory.
inline constexpr uint32 VERSION_1 = 1;
/// Version produced by the current writer.
inline constexpr uint32 CURRENT_VERSION = VERSION_1;

/// Spellings are strict UTF-8 without U+0000.
inline constexpr uint32 BYTE_POLICY_UTF8 = 1;
/// Spellings are opaque bytes without a zero byte; no text encoding is implied.
inline constexpr uint32 BYTE_POLICY_BYTES = 2;

/// Width of the producer-supplied content revision digest (for example SHA-256).
inline constexpr usize REVISION_DIGEST_BYTES = 32;
/// Directory index of the empty spelling in every dictionary.
inline constexpr uint32 EMPTY_ENTRY = 0;
/// Most directory entries, including the empty entry.
inline constexpr uint32 MAX_ENTRIES = uint32{1} << 24;
/// Longest spelling in bytes, excluding its terminator.
inline constexpr uint32 MAX_SPELLING_BYTES = 65535;
/// Largest spelling blob in bytes, including every terminator.
inline constexpr uint32 MAX_BLOB_BYTES = uint32{256} * 1024 * 1024;

/// Header field: MAGIC.size() bytes.
inline constexpr usize MAGIC_OFFSET = 0;
/// Header field: uint32 format version.
inline constexpr usize VERSION_OFFSET = MAGIC_OFFSET + MAGIC.size();
/// Header field: uint32 BYTE_POLICY_UTF8 or BYTE_POLICY_BYTES.
inline constexpr usize BYTE_POLICY_OFFSET = VERSION_OFFSET + sizeof(uint32);
/// Header field: uint32 directory entry count, 1..MAX_ENTRIES.
inline constexpr usize ENTRY_COUNT_OFFSET = BYTE_POLICY_OFFSET + sizeof(uint32);
/// Header field: uint32 directory size, exactly EntryCount * DIRECTORY_ENTRY_BYTES.
inline constexpr usize DIRECTORY_BYTES_OFFSET = ENTRY_COUNT_OFFSET + sizeof(uint32);
/// Header field: uint32 spelling blob size, at most MAX_BLOB_BYTES.
inline constexpr usize BLOB_BYTES_OFFSET = DIRECTORY_BYTES_OFFSET + sizeof(uint32);
/// Header field: uint32 reserved word; must be zero.
inline constexpr usize RESERVED_OFFSET = BLOB_BYTES_OFFSET + sizeof(uint32);
/// Header field: REVISION_DIGEST_BYTES identifying the content revision.
inline constexpr usize REVISION_DIGEST_OFFSET = RESERVED_OFFSET + sizeof(uint32);
/// Header size; the directory starts here.
inline constexpr usize HEADER_BYTES = REVISION_DIGEST_OFFSET + REVISION_DIGEST_BYTES;

/// Directory entry field: uint32 spelling offset from the start of the blob.
inline constexpr usize ENTRY_OFFSET_FIELD = 0;
/// Directory entry field: uint32 spelling length in bytes, excluding the terminator.
inline constexpr usize ENTRY_LENGTH_FIELD = ENTRY_OFFSET_FIELD + sizeof(uint32);
/// Size of one directory entry.
inline constexpr usize DIRECTORY_ENTRY_BYTES = ENTRY_LENGTH_FIELD + sizeof(uint32);

/// Largest admitted dictionary in bytes.
inline constexpr usize MAX_DICTIONARY_BYTES =
    HEADER_BYTES + usize{MAX_ENTRIES} * DIRECTORY_ENTRY_BYTES + usize{MAX_BLOB_BYTES};

static_assert(HEADER_BYTES == 64, "LUSTR v1 header layout changed");
static_assert(DIRECTORY_ENTRY_BYTES == 8, "LUSTR v1 directory entry layout changed");
static_assert(MAX_SPELLING_BYTES < MAX_BLOB_BYTES, "a maximal spelling must fit in the blob");
static_assert(MAX_DICTIONARY_BYTES < (usize{1} << 31), "dictionaries must be addressable on 32-bit targets");
} // namespace ludus::content::string_dictionary_format
