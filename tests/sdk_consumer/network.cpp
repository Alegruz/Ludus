#include <ludus/network/core/link_simulator.h>
#include <ludus/network/core/sequence.h>
#include <ludus/network/core/wire.h>

int ExerciseInstalledNetwork() noexcept
{
    using namespace ludus::network;
    OwnedMessage encoded;
    const uint8 payload[] = {3, 1, 4};
    if (EncodeMessage({ .Lane = Channel::Input, .Session = 1, .Sequence = 42 }, payload, encoded.Bytes, encoded.Size) !=
        WireStatus::Ok)
    {
        return 8;
    }
    SendQueue queue;
    LinkSimulator link;
    OwnedMessage received;
    if (queue.Enqueue(encoded.GetBytes()) != QueueStatus::Ok ||
        queue.Dequeue(MAX_MESSAGE_BYTES, received) != QueueStatus::Ok ||
        link.Send(0, received.GetBytes()) != LinkStatus::Ok || link.Receive(0, received) != LinkStatus::Ok)
    {
        return 8;
    }
    MessageView view;
    SequenceWindow window;
    if (DecodeMessage(received.GetBytes(), view) != WireStatus::Ok || view.Header.Sequence != 42 ||
        view.Payload.size() != 3 || view.Payload[2] != 4 || window.Observe(42) != SequenceStatus::Newest ||
        window.Observe(42) != SequenceStatus::Duplicate)
    {
        return 8;
    }
    return 0;
}
