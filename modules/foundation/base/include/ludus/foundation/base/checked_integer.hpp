#pragma once

// Opt-in checked arithmetic for the native Ludus integer aliases. Operands are
// copied; output may alias an operand and is unchanged on failure. No logging,
// assertions, allocation, saturation, or exceptions, including on overflow.
#include <ludus/foundation/base/types.h>

#include <type_traits>
#include <utility>

namespace ludus::foundation::core
{
template <typename ValueType>
concept UnsignedInteger =
    std::is_same_v<ValueType, uint8> || std::is_same_v<ValueType, uint16> || std::is_same_v<ValueType, uint32> ||
    std::is_same_v<ValueType, uint64> || std::is_same_v<ValueType, usize>;

template <typename ValueType>
concept Integer =
    UnsignedInteger<ValueType> || std::is_same_v<ValueType, int8> || std::is_same_v<ValueType, int16> ||
    std::is_same_v<ValueType, int32> || std::is_same_v<ValueType, int64> || std::is_same_v<ValueType, isize>;

// Bool, plain/Unicode characters, enums, floats, and qualified output types are
// intentionally excluded. An alias cannot distinguish int32 from native int.
template <Integer DestinationType, Integer SourceType>
[[nodiscard]] constexpr bool TryIntegerCast(SourceType value, DestinationType& out) noexcept
{
    if (!std::in_range<DestinationType>(value))
    {
        return false;
    }
    // NOLINTNEXTLINE(bugprone-signed-char-misuse): int8 is numeric; preserve negative values.
    out = static_cast<DestinationType>(value);
    return true;
}

// Generic overflow builtins have defined behavior even at signed limits and
// are constexpr on the validated native Clang and Emscripten toolchains.
template <Integer ValueType>
[[nodiscard]] constexpr bool TryAdd(ValueType left, ValueType right, ValueType& out) noexcept
{
    ValueType result{};
    if (__builtin_add_overflow(left, right, &result))
    {
        return false;
    }
    out = result;
    return true;
}

template <Integer ValueType>
[[nodiscard]] constexpr bool TrySubtract(ValueType left, ValueType right, ValueType& out) noexcept
{
    ValueType result{};
    if (__builtin_sub_overflow(left, right, &result))
    {
        return false;
    }
    out = result;
    return true;
}

template <Integer ValueType>
[[nodiscard]] constexpr bool TryMultiply(ValueType left, ValueType right, ValueType& out) noexcept
{
    ValueType result{};
    if (__builtin_mul_overflow(left, right, &result))
    {
        return false;
    }
    out = result;
    return true;
}
} // namespace ludus::foundation::core

namespace ludus::foundation
{
using core::Integer;
using core::TryAdd;
using core::TryIntegerCast;
using core::TryMultiply;
using core::TrySubtract;
using core::UnsignedInteger;
} // namespace ludus::foundation
