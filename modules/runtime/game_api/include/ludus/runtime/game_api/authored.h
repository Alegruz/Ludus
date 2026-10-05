#pragma once

#include <ludus/foundation/base/types.h>
#include <ludus/runtime/game_api/api.h>
#include <ludus/runtime/game_api/checkpoint.h>

namespace ludus::runtime::game_api
{
// Saved tuning JSON is validated by tooling, then encoded into this bounded
// transport: LTUN, version u32, game u64, count u32; records carry object u64,
// property u64, kind u32, byte count u32 and explicit little-endian payload.
// This is a transport version, independent of the module's property schema.
// Modules validate their own IDs/kinds/bounds before publishing an instance.
struct AuthoredRecord final
{
    uint64 Object = 0;
    uint64 Property = 0;
    uint32 Kind = 0;
    ByteView Value;
};

class AuthoredReader final
{
public:
    [[nodiscard]] bool Start(ByteView bytes, uint64 game) noexcept
    {
        if (bytes.Data == nullptr || bytes.Size < 20 || bytes.Size > usize{262144} || bytes.Data[0] != 'L' ||
            bytes.Data[1] != 'T' || bytes.Data[2] != 'U' || bytes.Data[3] != 'N' ||
            ReadCheckpointUint(bytes.Data + 4, 4) != 1 || ReadCheckpointUint(bytes.Data + 8, 8) != game)
        {
            return false;
        }
        const uint64 count = ReadCheckpointUint(bytes.Data + 16, 4);
        if (count > 4096)
        {
            return false;
        }
        Bytes_ = bytes;
        Offset_ = 20;
        Remaining_ = static_cast<uint32>(count);
        return true;
    }

    [[nodiscard]] bool Next(AuthoredRecord& record) noexcept
    {
        if (Remaining_ == 0 || Offset_ > Bytes_.Size || Bytes_.Size - Offset_ < 24)
        {
            return false;
        }
        const uint8* data = Bytes_.Data + Offset_;
        AuthoredRecord next;
        next.Object = ReadCheckpointUint(data, 8);
        next.Property = ReadCheckpointUint(data + 8, 8);
        next.Kind = static_cast<uint32>(ReadCheckpointUint(data + 16, 4));
        const usize length = static_cast<usize>(ReadCheckpointUint(data + 20, 4));
        if (next.Object == 0 || next.Property == 0 || next.Kind > 4 || length > 256 ||
            length > Bytes_.Size - Offset_ - 24)
        {
            return false;
        }
        next.Value = {data + 24, length};
        record = next;
        Offset_ += 24 + length;
        --Remaining_;
        return true;
    }

    [[nodiscard]] bool Complete() const noexcept
    {
        return Remaining_ == 0 && Offset_ == Bytes_.Size;
    }

private:
    ByteView Bytes_;
    usize Offset_ = 0;
    uint32 Remaining_ = 0;
};
} // namespace ludus::runtime::game_api
