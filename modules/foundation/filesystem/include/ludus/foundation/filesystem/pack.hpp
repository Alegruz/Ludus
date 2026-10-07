#pragma once

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/filesystem/namespace.hpp>

#include <string_view>

namespace ludus::foundation::filesystem
{
/// Maximum decoded bytes in one independent version-1 pack block.
inline constexpr uint32 MAX_PACK_BLOCK_BYTES = 65536;

/// Admission budgets for untrusted pack metadata, storage and decoded lengths.
/// Metadata budgets apply before allocating index tables; decoded-length budgets
/// apply before publication. No limit grants authenticity.
struct PackLimits final
{
    uint64 MaxPackBytes = 8ULL * 1024 * 1024 * 1024;     ///< Maximum captured archive size in bytes.
    uint64 MaxIndexBytes = 32ULL * 1024 * 1024;          ///< Maximum serialized index bytes retained at mount.
    uint64 MaxFileBytes = 1024ULL * 1024 * 1024;         ///< Maximum decoded length of any file in bytes.
    uint64 MaxDecodedBytes = 16ULL * 1024 * 1024 * 1024; ///< Maximum sum of all decoded file lengths.
    uint32 MaxFiles = 65536;                             ///< Maximum indexed file count.
    uint32 MaxBlocks = 262144;                           ///< Maximum indexed independent block count.
    uint32 MaxBlockBytes = MAX_PACK_BLOCK_BYTES;         ///< Maximum per-block decoded bytes, including scratch budget.
};

/// Opens and validates a version-1 pack through an existing byte provider on any target.
/// Retains the opened archive revision; failure preserves output. Path/source/limit
/// misuse returns InvalidArgument; malformed index/ranges/checksums return CorruptData;
/// unsupported version/flags/codecs return Unsupported; budget admission returns LimitExceeded.
/// Native storage faults retain their status and native code; allocation returns OutOfMemory.
/// All index paths, lengths, block ownership and nonoverlapping storage ranges are checked
/// before publication. Payload checksums and bounded LZ4 decode run on each touched block
/// before copying verified bytes. Payload faults do not trigger overlay fallback.
/// Reads allocate nothing; each opened file owns at most twice the advertised block size
/// in scratch bytes and serializes its concurrent reads. A Changed result has zero valid
/// bytes; other faults report only earlier verified progress. Empty/EOF reads check the
/// underlying archive revision. Index metadata is captured at mount; immutable publication
/// remains required because size/mtime and CRC32 cannot establish authenticity or defeat
/// hostile metadata-preserving mutation. See docs/architecture/filesystem.md.
[[nodiscard]] Result CreatePackProvider(const ProviderHandle& storage,
                                        std::string_view relativePath,
                                        const PackLimits& limits,
                                        ProviderHandle& output) noexcept;
} // namespace ludus::foundation::filesystem
