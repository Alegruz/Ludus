#pragma once

#include <ludus/foundation/base/types.h>
#include <ludus/runtime/game_api/api.h>

namespace ludus::runtime::game_api
{
// Flat tagged records: uint16 field ID, uint16 payload size, payload bytes.
// Integer/IEEE float bits use little endian, independently of native padding.
// Readers own schema, duplicate, required-field, type and semantic validation.
[[nodiscard]] inline bool WriteCheckpointUint(ByteSpan bytes, uint64 value) noexcept
{
    if (bytes.Data == nullptr || bytes.Capacity == 0 || bytes.Capacity > 8)
    {
        return false;
    }
    for (usize i = 0; i < bytes.Capacity; ++i)
    {
        bytes.Data[i] = static_cast<uint8>(value & 0xFFU);
        value >>= 8;
    }
    return value == 0;
}

[[nodiscard]] inline uint64 ReadCheckpointUint(const uint8* bytes, usize width) noexcept
{
    if (bytes == nullptr || width > 8)
    {
        return 0;
    }
    uint64 value = 0;
    for (usize i = 0; i < width; ++i)
    {
        value |= static_cast<uint64>(bytes[i]) << (i * 8);
    }
    return value;
}

struct CheckpointWriter final
{
    ByteSpan Buffer;
    usize Offset = 0;

    [[nodiscard]] bool Field(uint32 id, ByteView payload) noexcept
    {
        if (Buffer.Data == nullptr || payload.Data == nullptr || id == 0 || id > 65535 || payload.Size > 65535 ||
            Offset > Buffer.Capacity || Buffer.Capacity - Offset < 4 || payload.Size > Buffer.Capacity - Offset - 4)
        {
            return false;
        }
        if (!WriteCheckpointUint({Buffer.Data + Offset, 2}, id) ||
            !WriteCheckpointUint({Buffer.Data + Offset + 2, 2}, payload.Size))
        {
            return false;
        }
        for (usize i = 0; i < payload.Size; ++i)
        {
            Buffer.Data[Offset + 4 + i] = payload.Data[i];
        }
        Offset += 4 + payload.Size;
        return true;
    }
};

struct CheckpointReader final
{
    ByteView Buffer;
    usize Offset = 0;

    [[nodiscard]] bool Next(uint32& id, ByteView& payload) noexcept
    {
        if (Buffer.Data == nullptr || Offset > Buffer.Size || Buffer.Size - Offset < 4)
        {
            return false;
        }
        id = static_cast<uint32>(ReadCheckpointUint(Buffer.Data + Offset, 2));
        const usize length = static_cast<usize>(ReadCheckpointUint(Buffer.Data + Offset + 2, 2));
        if (id == 0 || length > Buffer.Size - Offset - 4)
        {
            return false;
        }
        payload = {Buffer.Data + Offset + 4, length};
        Offset += 4 + length;
        return true;
    }
};
} // namespace ludus::runtime::game_api
