#pragma once

#include <ludus/foundation/base/types.h>
#include <ludus/network/core/send_queue.h>

#include <span>

namespace ludus::network
{
inline constexpr usize SIMULATOR_CAPACITY = 32;

struct LinkConfig final
{
    uint64 DelayUs = 0;
    uint32 JitterUs = 0; // additional uniform [0, JitterUs] delay; can reorder
    uint16 LossPerTenThousand = 0;
    uint16 DuplicatePerTenThousand = 0;
    uint32 Seed = 1; // deterministic test seed, never a security RNG
};

enum class LinkStatus : uint8
{
    Ok,
    Dropped,
    InvalidArgument,
    InvalidTime,
    Full,
    Empty,
    TimeOverflow,
    SequenceExhausted,
};

struct LinkCounters final
{
    uint64 Accepted = 0;
    uint64 Dropped = 0;
    uint64 Duplicated = 0;
    uint64 Delivered = 0;
    uint64 RejectedFull = 0;
};

// One direction, explicitly advanced by caller time (microseconds). All valid
// Send/Receive calls, including Full/Empty results, advance the monotonic clock.
// Reset validates before clearing; bad configuration preserves the old link.
// Full/overflow leaves packet storage and RNG unchanged. Send copies opaque bytes
// (also supports malformed-message fixtures); Receive publishes owned bytes.
// Duplication admission is atomic: both copies must fit. No sleep/OS/thread/heap.
class LinkSimulator final
{
public:
    LinkStatus Reset(const LinkConfig& config) noexcept;
    LinkStatus Send(uint64 nowUs, std::span<const uint8> message) noexcept;
    LinkStatus Receive(uint64 nowUs, OwnedMessage& message) noexcept;
    [[nodiscard]] usize GetPendingCount() const noexcept
    {
        return mPending;
    }
    [[nodiscard]] const LinkCounters& GetCounters() const noexcept
    {
        return mCounters;
    }

private:
    struct Pending final
    {
        OwnedMessage Message;
        uint64 DueUs = 0;
        uint64 Order = 0;
        bool Active = false;
    };
    bool AdvanceTime(uint64 nowUs) noexcept;
    LinkConfig mConfig;
    Pending mPackets[SIMULATOR_CAPACITY];
    LinkCounters mCounters;
    uint64 mNowUs = 0;
    uint64 mOrder = 0;
    uint32 mRandom = 1;
    usize mPending = 0;
};
} // namespace ludus::network
