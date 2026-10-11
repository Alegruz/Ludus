#pragma once
#include <ludus/foundation/base/types.h>
namespace ludus::resources::internal
{
// Exhaustion permanently closes a slot; generation zero is never issued.
constexpr bool Advance(ludus::foundation::uint32& generation) noexcept
{
    if (generation == 0xFFFFFFFFU)
    {
        return false;
    }
    ++generation;
    return true;
}
} // namespace ludus::resources::internal
