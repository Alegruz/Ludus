#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::network
{
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;

enum class SequenceStatus : uint8
{
    Newest,
    OutOfOrder,
    Duplicate,
    TooOld,
    Ambiguous,
};

// Sliding 64-message duplicate filter, not a transport ACK or security mechanism.
// Serial arithmetic assumes a live sequence span < 2^31. The exactly-half-range
// case is rejected. Start a fresh window for every session/channel/direction.
class SequenceWindow final
{
public:
    SequenceStatus Observe(uint32 sequence) noexcept;
    void Reset() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept
    {
        return mInitialized;
    }
    [[nodiscard]] uint32 GetNewest() const noexcept
    {
        return mNewest;
    }
    [[nodiscard]] uint64 GetReceivedMask() const noexcept
    {
        return mReceived;
    }

private:
    uint64 mReceived = 0;
    uint32 mNewest = 0;
    bool mInitialized = false;
};
} // namespace ludus::network
