#include <ludus/network/core/send_queue.h>

#include <cstring>

namespace ludus::network
{
QueueStatus SendQueue::Enqueue(std::span<const uint8> message) noexcept
{
    MessageView view;
    if (DecodeMessage(message, view) != WireStatus::Ok)
    {
        return QueueStatus::InvalidMessage;
    }
    auto& lane = mLanes[static_cast<usize>(view.Header.Lane)];
    const bool replace = view.Header.Lane == Channel::Snapshot && lane.Count != 0;
    if (lane.Count == SEND_QUEUE_CAPACITY_PER_CHANNEL)
    {
        ++mCounters.RejectedFull;
        return QueueStatus::Full;
    }
    const usize index = replace ? lane.Head : (lane.Head + lane.Count) % SEND_QUEUE_CAPACITY_PER_CHANNEL;
    auto& stored = lane.Messages[index];
    std::memcpy(stored.Bytes, message.data(), message.size());
    stored.Size = message.size();
    ++mCounters.Enqueued;
    if (replace)
    {
        ++mCounters.ReplacedSnapshots;
        return QueueStatus::ReplacedSnapshot;
    }
    ++lane.Count;
    return QueueStatus::Ok;
}
QueueStatus SendQueue::Dequeue(usize byteBudget, OwnedMessage& message) noexcept
{
    bool pending = false;
    for (usize i = 0; i < CHANNEL_COUNT; ++i)
    {
        const usize index = (mNextLane + i) % CHANNEL_COUNT;
        auto& lane = mLanes[index];
        if (lane.Count == 0)
        {
            continue;
        }
        pending = true;
        const auto& front = lane.Messages[lane.Head];
        if (front.Size > byteBudget)
        {
            continue;
        }
        std::memcpy(message.Bytes, front.Bytes, front.Size);
        message.Size = front.Size;
        lane.Head = (lane.Head + 1) % SEND_QUEUE_CAPACITY_PER_CHANNEL;
        --lane.Count;
        mNextLane = (index + 1) % CHANNEL_COUNT;
        ++mCounters.Dequeued;
        return QueueStatus::Ok;
    }
    return pending ? QueueStatus::BudgetTooSmall : QueueStatus::Empty;
}
void SendQueue::Reset() noexcept
{
    for (auto& lane : mLanes)
    {
        lane.Head = 0;
        lane.Count = 0;
    }
    mNextLane = 0;
    mCounters = {};
}
usize SendQueue::GetPendingCount(Channel lane) const noexcept
{
    const auto index = static_cast<usize>(lane);
    return index < CHANNEL_COUNT ? mLanes[index].Count : 0;
}
} // namespace ludus::network
