#pragma once

// Bounded unsigned integer codecs. Only the first sizeof(T) bytes are consumed
// or written; trailing bytes are untouched. Truncation preserves output/buffer.
// Byte accesses support unaligned buffers without assuming native byte order.
// Thanks to Jason Hughes, "What to Look for When Evaluating Middleware for
// Integration", Game Engine Gems, section 1.13 "Platform Portability", p. 12,
// for the file/network endian audit lesson. These original codecs use bounded
// byte operations; no native object layout or chapter implementation is copied.
// Review: docs/architecture/primitive-types.md.
#include <ludus/foundation/base/types.h>

#include <ludus/foundation/base/checked_integer.hpp>

#include <span>

namespace ludus::foundation::core
{
namespace detail
{
template <bool LittleEndian, UnsignedInteger ValueType>
[[nodiscard]] constexpr bool TryReadEndian(std::span<const uint8> bytes, ValueType& out) noexcept
{
    if (bytes.size() < sizeof(ValueType))
    {
        return false;
    }
    ValueType result{};
    for (usize i = 0; i < sizeof(ValueType); ++i)
    {
        const usize index = LittleEndian ? i : sizeof(ValueType) - 1 - i;
        result = static_cast<ValueType>(result | (static_cast<ValueType>(bytes[index]) << (i * 8)));
    }
    out = result;
    return true;
}

template <bool LittleEndian, UnsignedInteger ValueType>
[[nodiscard]] constexpr bool TryWriteEndian(ValueType value, std::span<uint8> bytes) noexcept
{
    if (bytes.size() < sizeof(ValueType))
    {
        return false;
    }
    for (usize i = 0; i < sizeof(ValueType); ++i)
    {
        const usize index = LittleEndian ? i : sizeof(ValueType) - 1 - i;
        bytes[index] = static_cast<uint8>(value >> (i * 8));
    }
    return true;
}
} // namespace detail

template <UnsignedInteger ValueType>
[[nodiscard]] constexpr bool TryReadLittleEndian(std::span<const uint8> bytes, ValueType& out) noexcept
{
    return detail::TryReadEndian<true>(bytes, out);
}

template <UnsignedInteger ValueType>
[[nodiscard]] constexpr bool TryReadBigEndian(std::span<const uint8> bytes, ValueType& out) noexcept
{
    return detail::TryReadEndian<false>(bytes, out);
}

template <UnsignedInteger ValueType>
[[nodiscard]] constexpr bool TryWriteLittleEndian(ValueType value, std::span<uint8> bytes) noexcept
{
    return detail::TryWriteEndian<true>(value, bytes);
}

template <UnsignedInteger ValueType>
[[nodiscard]] constexpr bool TryWriteBigEndian(ValueType value, std::span<uint8> bytes) noexcept
{
    return detail::TryWriteEndian<false>(value, bytes);
}
} // namespace ludus::foundation::core

namespace ludus::foundation
{
using core::TryReadBigEndian;
using core::TryReadLittleEndian;
using core::TryWriteBigEndian;
using core::TryWriteLittleEndian;
} // namespace ludus::foundation
