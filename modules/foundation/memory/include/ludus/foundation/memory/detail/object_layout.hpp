#pragma once

// Checked allocation-layout validation for FoundationMemory object factories.
//
// Installed with the SDK because the factory templates in owned_object.hpp reach
// it at their point of instantiation. It is NOT part of the supported API:
// consumers include owned_object.hpp, never this file. The contract it serves is
// docs/architecture/smart-pointers.md ("Fallible construction and domain
// lifetime").
#include <ludus/foundation/base/types.h>

#include <ludus/foundation/base/checked_integer.hpp>

namespace ludus::foundation::detail
{
enum class ObjectLayoutCheck : uint8
{
    Valid,
    Malformed, // Zero size, or an alignment that is not a nonzero power of two.
    TooLarge,  // Rounding the size up to the alignment wraps or leaves the isize range.
};

// Validates one object's allocation request before any allocation, so that an
// aligned backend can round the exact size up to its alignment without wrapping
// or exceeding the signed address range.
[[nodiscard]] constexpr ObjectLayoutCheck CheckObjectLayout(usize bytes, usize alignment) noexcept
{
    if (bytes == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
    {
        return ObjectLayoutCheck::Malformed;
    }
    usize paddedBytes = 0;
    isize signedPaddedBytes = 0;
    if (TryAdd(bytes, alignment - 1, paddedBytes) == false || TryIntegerCast(paddedBytes, signedPaddedBytes) == false)
    {
        return ObjectLayoutCheck::TooLarge;
    }
    return ObjectLayoutCheck::Valid;
}
} // namespace ludus::foundation::detail
