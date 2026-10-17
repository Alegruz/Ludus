#pragma once

// Private RFC 4122 helpers matching Python's uuid module: str(UUID), the
// uuid.UUID(hex) constructor and uuid5 (SHA-1 name-based). SHA-1 lives here
// rather than in the public Content API because it only reproduces legacy
// logical identities; it is never a content hash.

#include "python_compat.h"

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/identity/uuid.hpp>

#include <span>
#include <string_view>

namespace ludus::content::scene_cook::detail
{
/// Byte length of a UUID.
inline constexpr usize UUID_BYTES = foundation::identity::UUID_BYTE_COUNT;
/// Character length of str(UUID).
inline constexpr usize UUID_TEXT_BYTES = foundation::identity::UUID_TEXT_LENGTH;

/// 128-bit identifier in big-endian (RFC 4122) byte order. FoundationIdentity's
/// value type, so fresh identities come from its single UUIDv4 construction path
/// (TryGenerateUuidV4); the helpers below only reproduce Python's text and uuid5 forms.
using Uuid = foundation::identity::Uuid;

/// SHA-1 digest of data (FIPS 180-4).
void ComputeSha1(std::span<const uint8> data, uint8 (&outDigest)[20]) noexcept;
/// str(uuid): lowercase 8-4-4-4-12 hexadecimal.
void AppendUuid(ByteWriter& inoutWriter, const Uuid& value) noexcept;
/// uuid.UUID(text) for a str argument; false where Python raises ValueError.
[[nodiscard]] bool ParseUuid(std::string_view text, Uuid& outValue) noexcept;
/// uuid.uuid5(namespace, name) for a UTF-8 name of at most 240 bytes.
[[nodiscard]] bool MakeNameUuid(const Uuid& space, std::string_view name, Uuid& outValue) noexcept;
} // namespace ludus::content::scene_cook::detail
