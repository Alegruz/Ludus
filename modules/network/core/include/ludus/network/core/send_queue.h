#pragma once

#include <ludus/foundation/base/types.h>
#include <ludus/network/core/wire.h>

#include <span>

namespace ludus::network
{
inline constexpr usize SEND_QUEUE_CAPACITY_PER_CHANNEL = 8;

struct OwnedMessage final
{
    uint8 Bytes[MAX_MESSAGE_BYTES] = {};
    usize Size = 0;
    [[nodiscard]] std::span<const uint8> GetBytes() const noexcept
    {
        return {Bytes, Size};
    }
};

enum class QueueStatus : uint8
{
    Ok,
    ReplacedSnapshot,
    InvalidMessage,
    Full,
    Empty,
    BudgetTooSmall,
};

struct QueueCounters final
{
    uint64 Enqueued = 0;
    uint64 Dequeued = 0;
    uint64 ReplacedSnapshots = 0;
    uint64 RejectedFull = 0;
};

// One connection/direction, one thread. Owns bytes; no heap or locks. Control,
// input and bulk are FIFO; snapshot is a single newest complete snapshot.
// Round-robin across nonempty lanes bounds starvation (message fairness).
// Budget includes the application envelope, not backend framing/IP overhead.
// On successful dequeue the caller owns the message and must retain it until
// the transport accepts it; a WouldBlock backend must never silently discard it.
class SendQueue final
{
public:
    QueueStatus Enqueue(std::span<const uint8> message) noexcept;
    QueueStatus Dequeue(usize byteBudget, OwnedMessage& message) noexcept;
    void Reset() noexcept;
    [[nodiscard]] usize GetPendingCount(Channel lane) const noexcept;
    [[nodiscard]] const QueueCounters& GetCounters() const noexcept
    {
        return mCounters;
    }

private:
    struct Lane final
    {
        OwnedMessage Messages[SEND_QUEUE_CAPACITY_PER_CHANNEL];
        usize Head = 0;
        usize Count = 0;
    };
    Lane mLanes[CHANNEL_COUNT];
    usize mNextLane = 0;
    QueueCounters mCounters;
};
} // namespace ludus::network
