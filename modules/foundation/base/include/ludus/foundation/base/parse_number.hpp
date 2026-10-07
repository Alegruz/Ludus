#pragma once
#include <ludus/foundation/base/types.h>
namespace ludus::foundation
{
/// Result of a bounded, locale-independent decimal conversion.
enum class NumberParseStatus : uint8
{
    /// The complete input was converted.
    Success,
    /// Input does not match the documented grammar.
    InvalidSyntax,
    /// The rounded result overflows or a nonzero input rounds to zero.
    OutOfRange,
    /// Input exceeds the supported 1024-byte bound.
    TooLong,
};
/// Parse ASCII JSON number syntax into binary32, rounding to nearest, ties to even.
/// @param text Input bytes; may be null only when size is zero. No whitespace,
/// leading plus, leading zeroes, hexadecimal, NaN or infinity is accepted.
/// @param size Number of bytes, at most 1024; the complete input must match.
/// @param output Updated only on success. Negative zero and subnormals are preserved.
/// @return Explicit syntax, length or range status; no allocations or exceptions.
/// @note Available on native and browser targets; independent of locale and the
/// floating-point environment. Uses bounded integer arithmetic, with no shared state.
[[nodiscard]] NumberParseStatus ParseFloat32(const char* text, usize size, float32& output) noexcept;
/// Parse a complete ASCII unsigned decimal integer into uint64.
/// @param text Input bytes; may be null only when size is zero. Digits only;
/// leading zeroes are accepted, signs and whitespace are rejected.
/// @param size Number of bytes, at most 1024.
/// @param output Updated only on success.
/// @return Explicit syntax, length or overflow status. No allocation or exceptions.
[[nodiscard]] NumberParseStatus ParseUint64(const char* text, usize size, uint64& output) noexcept;
} // namespace ludus::foundation
