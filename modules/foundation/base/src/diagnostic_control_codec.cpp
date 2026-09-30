#include <ludus/foundation/base/diagnostic_output.hpp>

namespace ludus::foundation::diagnostics
{
namespace
{
void PutU16(char* buffer, usize offset, uint16 value) noexcept
{
    buffer[offset + 0] = static_cast<char>(value & 0xFFu);
    buffer[offset + 1] = static_cast<char>((value >> 8) & 0xFFu);
}

void PutU32(char* buffer, usize offset, uint32 value) noexcept
{
    buffer[offset + 0] = static_cast<char>(value & 0xFFu);
    buffer[offset + 1] = static_cast<char>((value >> 8) & 0xFFu);
    buffer[offset + 2] = static_cast<char>((value >> 16) & 0xFFu);
    buffer[offset + 3] = static_cast<char>((value >> 24) & 0xFFu);
}

uint16 GetU16(const char* buffer, usize offset) noexcept
{
    return static_cast<uint16>(static_cast<uint8>(buffer[offset + 0])) |
           static_cast<uint16>(static_cast<uint16>(static_cast<uint8>(buffer[offset + 1])) << 8);
}

uint32 GetU32(const char* buffer, usize offset) noexcept
{
    return static_cast<uint32>(static_cast<uint8>(buffer[offset + 0])) |
           (static_cast<uint32>(static_cast<uint8>(buffer[offset + 1])) << 8) |
           (static_cast<uint32>(static_cast<uint8>(buffer[offset + 2])) << 16) |
           (static_cast<uint32>(static_cast<uint8>(buffer[offset + 3])) << 24);
}

} // namespace

usize EncodeControlHeader(char* buffer,
                          usize capacity,
                          ControlMessageType kind,
                          uint32 incidentId,
                          uint32 length) noexcept
{
    if (buffer == nullptr || capacity < CONTROL_HEADER_SIZE || length > CONTROL_MAX_PAYLOAD)
    {
        return 0;
    }
    PutU32(buffer, 0, CONTROL_PROTOCOL_MAGIC);
    PutU16(buffer, 4, CONTROL_PROTOCOL_VERSION);
    PutU16(buffer, 6, static_cast<uint16>(kind));
    PutU32(buffer, 8, incidentId);
    PutU32(buffer, 12, length);
    return CONTROL_HEADER_SIZE;
}

bool DecodeControlHeader(const char* buffer, usize size, ControlHeader& header) noexcept
{
    if (buffer == nullptr || size < CONTROL_HEADER_SIZE)
    {
        return false;
    }
    if (GetU32(buffer, 0) != CONTROL_PROTOCOL_MAGIC || GetU16(buffer, 4) != CONTROL_PROTOCOL_VERSION)
    {
        return false;
    }
    const uint16 kind = GetU16(buffer, 6);
    if (kind < static_cast<uint16>(ControlMessageType::Hello) ||
        kind > static_cast<uint16>(ControlMessageType::DecisionReply))
    {
        return false;
    }
    const uint32 length = GetU32(buffer, 12);
    if (length > CONTROL_MAX_PAYLOAD || CONTROL_HEADER_SIZE + length > size)
    {
        return false;
    }
    header.Kind = kind;
    header.IncidentId = GetU32(buffer, 8);
    header.Length = length;
    return true;
}

} // namespace ludus::foundation::diagnostics
