#include <ludus/network/core/link_simulator.h>
#include <ludus/network/core/send_queue.h>
#include <ludus/network/core/sequence.h>
#include <ludus/network/core/wire.h>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <span>

using namespace ludus::network;

namespace
{
OwnedMessage MakeMessage(Channel lane, uint32 sequence, usize payloadSize = 1)
{
    OwnedMessage result;
    uint8 payload[MAX_MESSAGE_BYTES] = {};
    payload[0] = static_cast<uint8>(sequence);
    REQUIRE(EncodeMessage({ .Lane = lane, .Session = 7, .Sequence = sequence },
                          {payload, payloadSize},
                          result.Bytes,
                          result.Size) == WireStatus::Ok);
    return result;
}
uint32 GetSequence(const OwnedMessage& message)
{
    MessageView view;
    REQUIRE(DecodeMessage(message.GetBytes(), view) == WireStatus::Ok);
    return view.Header.Sequence;
}
} // namespace

TEST_CASE("Wire bits have a platform-independent known answer and checked padding", "[network][wire]")
{
    uint8 bytes[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    BitWriter writer(bytes);
    REQUIRE(writer.WriteBits(5, 3) == WireStatus::Ok);
    REQUIRE(writer.WriteBits(17, 5) == WireStatus::Ok);
    REQUIRE(writer.WriteBits(0x12345678U, 32) == WireStatus::Ok);
    REQUIRE(writer.WriteBits(1, 1) == WireStatus::Ok);
    REQUIRE(writer.AlignToByte() == WireStatus::Ok);
    const uint8 expected[] = {0x8d, 0x78, 0x56, 0x34, 0x12, 0x01};
    REQUIRE(std::memcmp(bytes, expected, sizeof(bytes)) == 0);
    REQUIRE(writer.GetBitCount() == 48);
    BitReader reader(bytes);
    uint32 value = 0;
    REQUIRE(reader.ReadBits(3, value) == WireStatus::Ok);
    REQUIRE(value == 5);
    REQUIRE(reader.ReadBits(5, value) == WireStatus::Ok);
    REQUIRE(value == 17);
    REQUIRE(reader.ReadBits(32, value) == WireStatus::Ok);
    REQUIRE(value == 0x12345678U);
    REQUIRE(reader.ReadBits(1, value) == WireStatus::Ok);
    REQUIRE(value == 1);
    bytes[5] |= 0x80;
    REQUIRE(reader.AlignToByte() == WireStatus::MalformedMessage);
    REQUIRE(reader.GetBitCount() == 41);
    bytes[5] = 1;
    REQUIRE(reader.AlignToByte() == WireStatus::Ok);
}
TEST_CASE("All bit widths cross byte boundaries without changing neighboring bits", "[network][wire]")
{
    for (uint8 offset = 0; offset < 8; ++offset)
    {
        for (uint8 width = 0; width <= 32; ++width)
        {
            uint8 bytes[8];
            std::memset(bytes, 0xff, sizeof(bytes));
            const uint32 value = width == 32 ? 0xa55af00fU : 0xa55af00fU & ((uint32{1} << width) - 1);
            BitWriter writer(bytes);
            REQUIRE(writer.WriteBits(0, offset) == WireStatus::Ok);
            REQUIRE(writer.WriteBits(value, width) == WireStatus::Ok);
            for (usize bit = offset + width; bit < sizeof(bytes) * 8; ++bit)
            {
                REQUIRE(((bytes[bit / 8] >> (bit % 8)) & 1U) == 1);
            }
            BitReader reader(bytes);
            uint32 result = 999;
            REQUIRE(reader.ReadBits(offset, result) == WireStatus::Ok);
            REQUIRE(result == 0);
            REQUIRE(reader.ReadBits(width, result) == WireStatus::Ok);
            REQUIRE(result == value);
        }
    }
}
TEST_CASE("Wire failures preserve cursor, output and canaries", "[network][wire]")
{
    uint8 bytes[] = {0xa5, 0xa5, 0xa5};
    BitWriter writer(std::span<uint8>(bytes).subspan(1, 1));
    REQUIRE(writer.WriteBits(256, 8) == WireStatus::InvalidArgument);
    REQUIRE(writer.WriteBits(1, 0) == WireStatus::InvalidArgument);
    REQUIRE(writer.WriteBits(0, 33) == WireStatus::InvalidArgument);
    REQUIRE(writer.WriteBits(0x1234, 16) == WireStatus::BufferTooSmall);
    REQUIRE(writer.GetBitCount() == 0);
    REQUIRE(bytes[1] == 0xa5);
    REQUIRE(writer.WriteBits(3, 2) == WireStatus::Ok);
    const uint8 saved = bytes[1];
    REQUIRE(writer.WriteBytes({bytes, 1}) == WireStatus::InvalidArgument);
    REQUIRE(writer.WriteBits(255, 8) == WireStatus::BufferTooSmall);
    REQUIRE(writer.GetBitCount() == 2);
    REQUIRE(bytes[1] == saved);
    REQUIRE(writer.AlignToByte() == WireStatus::Ok);
    REQUIRE(writer.WriteBytes({bytes, 1}) == WireStatus::BufferTooSmall);
    REQUIRE(writer.WriteBits(0, 0) == WireStatus::Ok);
    REQUIRE(bytes[0] == 0xa5);
    REQUIRE(bytes[2] == 0xa5);
    BitReader reader(std::span<const uint8>(bytes).subspan(1, 1));
    uint32 value = 42;
    REQUIRE(reader.ReadBits(9, value) == WireStatus::BufferTooSmall);
    REQUIRE(reader.ReadBits(33, value) == WireStatus::InvalidArgument);
    REQUIRE(reader.GetBitCount() == 0);
    REQUIRE(value == 42);
    uint8 destination[] = {9, 9};
    REQUIRE(reader.ReadBytes(destination) == WireStatus::BufferTooSmall);
    REQUIRE(destination[0] == 9);
    REQUIRE(destination[1] == 9);
    REQUIRE(reader.ReadBits(2, value) == WireStatus::Ok);
    REQUIRE(reader.ReadBytes(destination) == WireStatus::InvalidArgument);
    BitWriter emptyWriter({});
    BitReader emptyReader({});
    REQUIRE(emptyWriter.WriteBits(0, 0) == WireStatus::Ok);
    REQUIRE(emptyReader.ReadBits(0, value) == WireStatus::Ok);
    REQUIRE(value == 0);
    REQUIRE(emptyWriter.WriteBytes({}) == WireStatus::Ok);
    REQUIRE(emptyReader.ReadBytes({}) == WireStatus::Ok);
}
TEST_CASE("Aligned bytes copy through the checked wire streams", "[network][wire]")
{
    uint8 bytes[6] = {};
    const uint8 source[] = {2, 4, 8, 16};
    uint8 result[4] = {};
    BitWriter writer(bytes);
    REQUIRE(writer.WriteBits(3, 2) == WireStatus::Ok);
    REQUIRE(writer.AlignToByte() == WireStatus::Ok);
    REQUIRE(writer.WriteBytes(source) == WireStatus::Ok);
    BitReader reader(bytes);
    uint32 value = 0;
    REQUIRE(reader.ReadBits(2, value) == WireStatus::Ok);
    REQUIRE(reader.AlignToByte() == WireStatus::Ok);
    REQUIRE(reader.ReadBytes(result) == WireStatus::Ok);
    REQUIRE(std::memcmp(source, result, sizeof(source)) == 0);
}
TEST_CASE("Envelope golden vector fixes layout independently of C++ object representation", "[network][message]")
{
    uint8 buffer[64] = {};
    const uint8 payload[] = {0x5a, 0xa5};
    usize written = 999;
    const MessageHeader header
    {
        .Lane = Channel::Snapshot,
        .Kind = 0x1234,
        .Session = 0x0102030405060708ULL,
        .Sequence = 0x11223344U,
    };
    REQUIRE(EncodeMessage(header, payload, buffer, written) == WireStatus::Ok);
    const uint8 expected[] = {'L', 'N', 'E', 'T',  1,    2,    0x34, 0x12, 8, 7, 6, 5,    4,
                              3,   2,   1,   0x44, 0x33, 0x22, 0x11, 2,    0, 0, 0, 0x5a, 0xa5};
    REQUIRE(written == sizeof(expected));
    REQUIRE(std::memcmp(buffer, expected, written) == 0);
    MessageView view;
    REQUIRE(DecodeMessage({buffer, written}, view) == WireStatus::Ok);
    REQUIRE(view.Header.Session == header.Session);
    REQUIRE(view.Header.Kind == header.Kind);
    REQUIRE(view.Header.Lane == header.Lane);
    REQUIRE(view.Header.Sequence == header.Sequence);
    REQUIRE(view.Payload.data() == buffer + MESSAGE_HEADER_BYTES);
    REQUIRE(view.Payload.size() == 2);
}
TEST_CASE("Envelope rejects truncation, reserved values, oversized and mismatched lengths transactionally",
          "[network][message]")
{
    const auto valid = MakeMessage(Channel::Control, 10);
    const MessageView sentinel{ .Header = { .Kind = 42, .Session = 99 }, .Payload = {} };
    MessageView view = sentinel;
    for (usize size = 0; size < valid.Size; ++size)
    {
        REQUIRE(DecodeMessage(valid.GetBytes().first(size), view) == WireStatus::MalformedMessage);
        REQUIRE(view.Header.Kind == sentinel.Header.Kind);
        REQUIRE(view.Header.Session == 99);
    }
    for (const usize offset : {usize{0}, usize{5}, usize{6}, usize{8}, usize{20}, usize{22}})
    {
        auto broken = valid;
        if (offset == 6 || offset == 8)
        {
            broken.Bytes[offset] = 0;
        }
        else
        {
            broken.Bytes[offset] = 255;
        }
        REQUIRE(DecodeMessage(broken.GetBytes(), view) == WireStatus::MalformedMessage);
    }
    auto broken = valid;
    broken.Bytes[4] = 2;
    REQUIRE(DecodeMessage(broken.GetBytes(), view) == WireStatus::UnsupportedVersion);
    broken = valid;
    ++broken.Size;
    REQUIRE(DecodeMessage(broken.GetBytes(), view) == WireStatus::MalformedMessage);
    broken.Size = MAX_MESSAGE_BYTES + 1;
    // Valid backing storage for oversized-span test.
    const uint8 large[MAX_MESSAGE_BYTES + 1] = {};
    REQUIRE(DecodeMessage(large, view) == WireStatus::MalformedMessage);
    uint8 destination[32];
    std::memset(destination, 0xa5, sizeof(destination));
    usize written = 123;
    REQUIRE(EncodeMessage({ .Session = 0 }, {}, destination, written) == WireStatus::InvalidArgument);
    REQUIRE(EncodeMessage({ .Lane = static_cast<Channel>(255), .Session = 1 }, {}, destination, written) ==
            WireStatus::InvalidArgument);
    REQUIRE(EncodeMessage({ .Kind = 0, .Session = 1 }, {}, destination, written) == WireStatus::InvalidArgument);
    REQUIRE(EncodeMessage({ .Session = 1 }, {}, {destination, 23}, written) == WireStatus::BufferTooSmall);
    REQUIRE(written == 123);
    for (auto byte : destination)
    {
        REQUIRE(byte == 0xa5);
    }
    OwnedMessage max;
    REQUIRE(EncodeMessage({ .Session = 1 },
                          std::span<const uint8>(large).first(MAX_MESSAGE_BYTES - MESSAGE_HEADER_BYTES),
                          max.Bytes,
                          max.Size) == WireStatus::Ok);
    REQUIRE(max.Size == MAX_MESSAGE_BYTES);
    REQUIRE(DecodeMessage(max.GetBytes(), view) == WireStatus::Ok);
    REQUIRE(EncodeMessage({ .Session = 1 }, large, max.Bytes, max.Size) == WireStatus::InvalidArgument);
}
TEST_CASE("Duplicate window covers wrap, exact 64 boundary and half-range ambiguity", "[network][sequence]")
{
    SequenceWindow window;
    REQUIRE_FALSE(window.IsInitialized());
    REQUIRE(window.Observe(0xfffffffeU) == SequenceStatus::Newest);
    REQUIRE(window.Observe(0) == SequenceStatus::Newest);
    REQUIRE(window.Observe(0xffffffffU) == SequenceStatus::OutOfOrder);
    REQUIRE(window.GetReceivedMask() == 7);
    REQUIRE(window.Observe(0xffffffffU) == SequenceStatus::Duplicate);
    REQUIRE(window.Observe(0x80000000U) == SequenceStatus::Ambiguous);
    REQUIRE(window.GetReceivedMask() == 7);
    REQUIRE(window.Observe(63) == SequenceStatus::Newest);
    REQUIRE(window.Observe(0) == SequenceStatus::Duplicate);
    REQUIRE(window.Observe(0xffffffffU) == SequenceStatus::TooOld);
    REQUIRE(window.Observe(127) == SequenceStatus::Newest);
    REQUIRE(window.GetReceivedMask() == 1);
    REQUIRE(window.Observe(64) == SequenceStatus::OutOfOrder);
    REQUIRE(window.Observe(63) == SequenceStatus::TooOld);
    window.Reset();
    REQUIRE_FALSE(window.IsInitialized());
    REQUIRE(window.Observe(0x80000000U) == SequenceStatus::Newest);
}
TEST_CASE("Queue isolates lane capacity, owns input bytes and coalesces snapshots", "[network][queue]")
{
    SendQueue queue;
    for (uint32 i = 0; i < SEND_QUEUE_CAPACITY_PER_CHANNEL; ++i)
    {
        REQUIRE(queue.Enqueue(MakeMessage(Channel::Bulk, i).GetBytes()) == QueueStatus::Ok);
    }
    REQUIRE(queue.Enqueue(MakeMessage(Channel::Bulk, 9).GetBytes()) == QueueStatus::Full);
    REQUIRE(queue.Enqueue(MakeMessage(Channel::Input, 10).GetBytes()) == QueueStatus::Ok);
    auto snapshot = MakeMessage(Channel::Snapshot, 1);
    REQUIRE(queue.Enqueue(snapshot.GetBytes()) == QueueStatus::Ok);
    snapshot = MakeMessage(Channel::Snapshot, 2);
    REQUIRE(queue.Enqueue(snapshot.GetBytes()) == QueueStatus::ReplacedSnapshot);
    std::memset(snapshot.Bytes, 0, snapshot.Size);
    REQUIRE(queue.GetPendingCount(Channel::Snapshot) == 1);
    REQUIRE(queue.GetPendingCount(static_cast<Channel>(255)) == 0);
    OwnedMessage output;
    output.Size = 999;
    REQUIRE(queue.Dequeue(1, output) == QueueStatus::BudgetTooSmall);
    REQUIRE(output.Size == 999);
    REQUIRE(queue.Dequeue(MAX_MESSAGE_BYTES, output) == QueueStatus::Ok);
    REQUIRE(GetSequence(output) == 10);
    REQUIRE(queue.Dequeue(MAX_MESSAGE_BYTES, output) == QueueStatus::Ok);
    REQUIRE(GetSequence(output) == 2);
    for (uint32 i = 0; i < SEND_QUEUE_CAPACITY_PER_CHANNEL; ++i)
    {
        REQUIRE(queue.Dequeue(MAX_MESSAGE_BYTES, output) == QueueStatus::Ok);
        REQUIRE(GetSequence(output) == i);
    }
    REQUIRE(queue.GetCounters().ReplacedSnapshots == 1);
    REQUIRE(queue.GetCounters().RejectedFull == 1);
    REQUIRE(queue.Dequeue(MAX_MESSAGE_BYTES, output) == QueueStatus::Empty);
    REQUIRE(queue.Enqueue({}) == QueueStatus::InvalidMessage);
    queue.Reset();
    REQUIRE(queue.GetCounters().Enqueued == 0);
}
TEST_CASE("Round-robin prevents bulk starvation and skips heads larger than remaining budget", "[network][queue]")
{
    SendQueue queue;
    for (usize lane = 0; lane < CHANNEL_COUNT; ++lane)
    {
        REQUIRE(queue.Enqueue(MakeMessage(static_cast<Channel>(lane), static_cast<uint32>(lane)).GetBytes()) ==
                QueueStatus::Ok);
    }
    OwnedMessage output;
    for (uint32 lane = 0; lane < CHANNEL_COUNT; ++lane)
    {
        REQUIRE(queue.Dequeue(MAX_MESSAGE_BYTES, output) == QueueStatus::Ok);
        REQUIRE(GetSequence(output) == lane);
    }
    REQUIRE(queue.Enqueue(MakeMessage(Channel::Control, 10, 50).GetBytes()) == QueueStatus::Ok);
    REQUIRE(queue.Enqueue(MakeMessage(Channel::Input, 20).GetBytes()) == QueueStatus::Ok);
    REQUIRE(queue.Dequeue(MESSAGE_HEADER_BYTES + 1, output) == QueueStatus::Ok);
    REQUIRE(GetSequence(output) == 20);
    REQUIRE(queue.Dequeue(MESSAGE_HEADER_BYTES + 1, output) == QueueStatus::BudgetTooSmall);
    REQUIRE(queue.Dequeue(MAX_MESSAGE_BYTES, output) == QueueStatus::Ok);
    REQUIRE(GetSequence(output) == 10);
    // Repeated ring reuse must preserve FIFO through wrapped head indices.
    for (uint32 i = 0; i < 100; ++i)
    {
        REQUIRE(queue.Enqueue(MakeMessage(Channel::Control, i).GetBytes()) == QueueStatus::Ok);
        REQUIRE(queue.Dequeue(MAX_MESSAGE_BYTES, output) == QueueStatus::Ok);
        REQUIRE(GetSequence(output) == i);
    }
}
TEST_CASE("Simulator timing, duplicate ownership, loss, atomic capacity and rollback", "[network][simulator]")
{
    LinkSimulator link;
    REQUIRE(link.Reset({ .DelayUs = 10, .DuplicatePerTenThousand = 10000 }) == LinkStatus::Ok);
    auto message = MakeMessage(Channel::Input, 42);
    REQUIRE(link.Send(5, message.GetBytes()) == LinkStatus::Ok);
    std::memset(message.Bytes, 0, message.Size);
    OwnedMessage output;
    output.Size = 999;
    REQUIRE(link.Receive(14, output) == LinkStatus::Empty);
    REQUIRE(output.Size == 999);
    REQUIRE(link.Receive(13, output) == LinkStatus::InvalidTime);
    REQUIRE(link.Receive(15, output) == LinkStatus::Ok);
    REQUIRE(GetSequence(output) == 42);
    REQUIRE(link.Receive(15, output) == LinkStatus::Ok);
    REQUIRE(GetSequence(output) == 42);
    REQUIRE(link.GetCounters().Duplicated == 1);
    REQUIRE(link.GetCounters().Delivered == 2);
    REQUIRE(link.Reset({ .LossPerTenThousand = 10000 }) == LinkStatus::Ok);
    message = MakeMessage(Channel::Input, 1);
    REQUIRE(link.Send(0, message.GetBytes()) == LinkStatus::Dropped);
    REQUIRE(link.GetPendingCount() == 0);
    REQUIRE(link.GetCounters().Dropped == 1);
    REQUIRE(link.Reset({ .DelayUs = 100 }) == LinkStatus::Ok);
    for (usize i = 0; i < SIMULATOR_CAPACITY; ++i)
    {
        REQUIRE(link.Send(0, message.GetBytes()) == LinkStatus::Ok);
    }
    REQUIRE(link.Send(0, message.GetBytes()) == LinkStatus::Full);
    REQUIRE(link.Reset({ .LossPerTenThousand = 10001 }) == LinkStatus::InvalidArgument);
    REQUIRE(link.GetPendingCount() == SIMULATOR_CAPACITY);
    REQUIRE(link.Reset({ .DelayUs = ~uint64{0}, .JitterUs = 1 }) == LinkStatus::InvalidArgument);
    REQUIRE(link.Reset({ .DelayUs = 1 }) == LinkStatus::Ok);
    REQUIRE(link.Send(~uint64{0}, message.GetBytes()) == LinkStatus::TimeOverflow);
    REQUIRE(link.GetPendingCount() == 0);
    REQUIRE(link.Send(~uint64{0} - 1, message.GetBytes()) == LinkStatus::InvalidTime);
    REQUIRE(link.Reset({}) == LinkStatus::Ok);
    REQUIRE(link.Send(0, {}) == LinkStatus::InvalidArgument);
}
TEST_CASE("Seeded fault traces are reproducible and include reordering", "[network][simulator]")
{
    LinkSimulator first;
    LinkSimulator second;
    const LinkConfig config
    {
        .JitterUs = 1000,
        .LossPerTenThousand = 2500,
        .DuplicatePerTenThousand = 2500,
        .Seed = 123,
    };
    REQUIRE(first.Reset(config) == LinkStatus::Ok);
    REQUIRE(second.Reset(config) == LinkStatus::Ok);
    for (uint32 i = 0; i < 16; ++i)
    {
        const auto message = MakeMessage(Channel::Input, i);
        REQUIRE(first.Send(i, message.GetBytes()) == second.Send(i, message.GetBytes()));
    }
    OwnedMessage left;
    OwnedMessage right;
    bool reordered = false;
    uint32 previous = 0;
    usize count = 0;
    for (;;)
    {
        const auto a = first.Receive(2000, left);
        const auto b = second.Receive(2000, right);
        REQUIRE(a == b);
        if (a == LinkStatus::Empty)
        {
            break;
        }
        REQUIRE(a == LinkStatus::Ok);
        REQUIRE(left.Size == right.Size);
        REQUIRE(std::memcmp(left.Bytes, right.Bytes, left.Size) == 0);
        const auto sequence = GetSequence(left);
        if (count != 0 && sequence < previous)
        {
            reordered = true;
        }
        previous = sequence;
        ++count;
    }
    REQUIRE(count > 0);
    REQUIRE(reordered);
    REQUIRE(first.GetCounters().Dropped > 0);
}
TEST_CASE("Encode, schedule, fault link and duplicate filter compose end to end", "[network][integration]")
{
    SendQueue queue;
    LinkSimulator link;
    SequenceWindow received;
    REQUIRE(link.Reset({ .DelayUs = 100, .DuplicatePerTenThousand = 10000 }) == LinkStatus::Ok);
    for (uint32 i = 0; i < 8; ++i)
    {
        REQUIRE(queue.Enqueue(MakeMessage(Channel::Input, i).GetBytes()) == QueueStatus::Ok);
    }
    OwnedMessage output;
    usize byteBudget = 8 * (MESSAGE_HEADER_BYTES + 1);
    while (queue.Dequeue(byteBudget, output) == QueueStatus::Ok)
    {
        byteBudget -= output.Size;
        REQUIRE(link.Send(0, output.GetBytes()) == LinkStatus::Ok);
    }
    REQUIRE(byteBudget == 0);
    usize accepted = 0;
    usize duplicates = 0;
    while (link.Receive(100, output) == LinkStatus::Ok)
    {
        MessageView view;
        REQUIRE(DecodeMessage(output.GetBytes(), view) == WireStatus::Ok);
        REQUIRE(view.Header.Session == 7);
        const auto status = received.Observe(view.Header.Sequence);
        if (status == SequenceStatus::Newest)
        {
            ++accepted;
        }
        else
        {
            REQUIRE(status == SequenceStatus::Duplicate);
            ++duplicates;
        }
    }
    REQUIRE(accepted == 8);
    REQUIRE(duplicates == 8);
}

TEST_CASE("Simulator duplicate admission requires two free slots without losing queued traffic", "[network][simulator]")
{
    LinkSimulator link;
    REQUIRE(link.Reset({ .DelayUs = 100, .DuplicatePerTenThousand = 10000 }) == LinkStatus::Ok);
    const auto message = MakeMessage(Channel::Input, 9);
    for (usize i = 0; i < SIMULATOR_CAPACITY / 2; ++i)
    {
        REQUIRE(link.Send(0, message.GetBytes()) == LinkStatus::Ok);
    }
    OwnedMessage output;
    REQUIRE(link.Receive(100, output) == LinkStatus::Ok);
    REQUIRE(link.GetPendingCount() == SIMULATOR_CAPACITY - 1);
    REQUIRE(link.Send(100, message.GetBytes()) == LinkStatus::Full);
    REQUIRE(link.GetPendingCount() == SIMULATOR_CAPACITY - 1);
    usize delivered = 1;
    while (link.Receive(100, output) == LinkStatus::Ok)
    {
        REQUIRE(GetSequence(output) == 9);
        ++delivered;
    }
    REQUIRE(delivered == SIMULATOR_CAPACITY);
}
TEST_CASE("Malformed-envelope mutation corpus never partially publishes decoded state", "[network][message]")
{
    uint32 random = 0x12345678;
    for (usize attempt = 0; attempt < 2048; ++attempt)
    {
        auto message = MakeMessage(Channel::Snapshot, 42, 17);
        random = random * 1664525U + 1013904223U;
        const usize offset = random % message.Size;
        random = random * 1664525U + 1013904223U;
        message.Bytes[offset] ^= static_cast<uint8>(random >> 24);
        MessageView view{ .Header = { .Kind = 999, .Session = 888 }, .Payload = {} };
        const auto status = DecodeMessage(message.GetBytes(), view);
        if (status != WireStatus::Ok)
        {
            REQUIRE(view.Header.Kind == 999);
            REQUIRE(view.Header.Session == 888);
            REQUIRE(view.Payload.empty());
        }
        else
        {
            REQUIRE(view.Payload.size() == 17);
            REQUIRE(view.Payload.data() == message.Bytes + MESSAGE_HEADER_BYTES);
            REQUIRE(static_cast<usize>(view.Header.Lane) < CHANNEL_COUNT);
        }
    }
}
