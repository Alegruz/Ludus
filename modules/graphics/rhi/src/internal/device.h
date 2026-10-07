#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::graphics::rhi::internal
{
// Process identities are never reused, including after failed/lost sessions.
// Kept separate from callback-attempt tokens so Auto retries keep one owner.
struct DeviceOwnerSequence final
{
    ludus::foundation::uint64 Next = 1;

    ludus::foundation::uint64 Take() noexcept
    {
        const auto owner = Next;
        Next = owner == ~ludus::foundation::uint64{0} ? 0 : owner == 0 ? 0 : owner + 1;
        return owner;
    }
};
} // namespace ludus::graphics::rhi::internal
