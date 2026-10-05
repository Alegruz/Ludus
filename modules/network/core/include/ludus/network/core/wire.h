#pragma once

#include <ludus/foundation/base/types.h>

#include <span>

namespace ludus::network
{
using ludus::foundation::uint16;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;
using ludus::foundation::usize;

// Application message bound, including our envelope; not a transport path MTU.
inline constexpr usize MAX_MESSAGE_BYTES = 1200;
inline constexpr usize MESSAGE_HEADER_BYTES = 24;
inline constexpr uint8 WIRE_VERSION = 1;
inline constexpr usize CHANNEL_COUNT = 4;

enum class Channel : uint8
{
    Control,
    Input,
    Snapshot,
    Bulk,
};

enum class WireStatus : uint8
{
    Ok,
    InvalidArgument,
    BufferTooSmall,
    MalformedMessage,
    UnsupportedVersion,
};

// Each primitive preflights its entire operation. Failure preserves cursor,
// output and backing bytes. Bits are LSB first; integers therefore little endian.
// AlignToByte writes/checks zero padding. Byte operations require alignment.
// Buffers must outlive their stream. Byte copy input/output must not overlap.
class BitWriter final
{
public:
    explicit BitWriter(std::span<uint8> bytes) noexcept : mBytes(bytes) {}
    WireStatus WriteBits(uint32 value, uint8 count) noexcept;
    WireStatus AlignToByte() noexcept;
    WireStatus WriteBytes(std::span<const uint8> bytes) noexcept;
    [[nodiscard]] usize GetBitCount() const noexcept
    {
        return mBitCount;
    }
    [[nodiscard]] usize GetByteCount() const noexcept
    {
        return mBitCount / 8 + (mBitCount % 8 != 0 ? 1 : 0);
    }

private:
    std::span<uint8> mBytes;
    usize mBitCount = 0;
};

class BitReader final
{
public:
    explicit BitReader(std::span<const uint8> bytes) noexcept : mBytes(bytes) {}
    WireStatus ReadBits(uint8 count, uint32& value) noexcept;
    WireStatus AlignToByte() noexcept;
    WireStatus ReadBytes(std::span<uint8> bytes) noexcept;
    [[nodiscard]] usize GetBitCount() const noexcept
    {
        return mBitCount;
    }

private:
    std::span<const uint8> mBytes;
    usize mBitCount = 0;
};

struct MessageHeader final
{
    Channel Lane = Channel::Control;
    uint16 Kind = 1;     // game schema identifier; zero is reserved
    uint64 Session = 0;  // nonzero lifecycle epoch, NOT authentication
    uint32 Sequence = 0; // independent sequence per channel and direction
};

struct MessageView final
{
    MessageHeader Header;
    std::span<const uint8> Payload; // borrowed from the encoded message
};

// Exact length, magic, version, lane, reserved fields and epoch are checked.
// Failure leaves destination/written/view unchanged. Encode buffers and payload
// must not overlap. Decode never asserts on untrusted bytes or allocates.
WireStatus EncodeMessage(const MessageHeader& header,
                         std::span<const uint8> payload,
                         std::span<uint8> destination,
                         usize& written) noexcept;
WireStatus DecodeMessage(std::span<const uint8> bytes, MessageView& view) noexcept;
} // namespace ludus::network
