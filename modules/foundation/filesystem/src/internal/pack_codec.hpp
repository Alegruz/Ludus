#pragma once

#include <ludus/foundation/base/types.h>

#include <span>

namespace ludus::foundation::filesystem::internal
{
uint32 Crc32(std::span<const uint8> bytes) noexcept;
bool DecodeLz4(std::span<const uint8> stored, std::span<uint8> decoded) noexcept;
} // namespace ludus::foundation::filesystem::internal
