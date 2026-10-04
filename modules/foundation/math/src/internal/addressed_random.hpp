#pragma once

// Private scalar reference and injection seam for exhaustive retry tests.
#include <ludus/foundation/math/addressed_random.hpp>

namespace ludus::foundation::math::internal
{
[[nodiscard]] RandomBlock Philox4x32(RandomBlock counter, RandomKey key) noexcept;
[[nodiscard]] RandomBlock PackRandomCounter(RandomAddress address, uint16 attempt) noexcept;
[[nodiscard]] float32 RandomFloat01(uint32 bits) noexcept;

// Fixed retry budget is part of layout v1. Keep the loop index wider than the
// encoded attempt so attempt 65535 does not wrap back to zero.
template <typename Draw>
[[nodiscard]] MathStatus SampleBounded(PreparedBound32 bound, uint32& out, Draw draw) noexcept
{
    for (uint32 attempt = 0; attempt < 65536u; ++attempt)
    {
        const uint64 product = static_cast<uint64>(draw(static_cast<uint16>(attempt))) * bound.Bound();
        if (static_cast<uint32>(product) >= bound.Threshold())
        {
            out = static_cast<uint32>(product >> 32u);
            return MathStatus::Success;
        }
    }
    return MathStatus::OutOfRange;
}
} // namespace ludus::foundation::math::internal
