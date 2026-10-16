// RFC 9562 sections 4 and 5.4 (K. Davis, B. Peabody and P. Leach) define the
// byte order, text groups and version/variant fields implemented here; see the
// attribution in uuid.hpp.
#include <ludus/foundation/base/core.h>

#include <ludus/foundation/identity/uuid.hpp>

namespace ludus::foundation::identity
{
namespace
{
/// Byte offsets after which the canonical text form places a separator.
constexpr usize SEPARATOR_AFTER_BYTES[] = {4, 6, 8, 10};
/// Version 4 in the high nibble of the version byte.
constexpr uint8 VERSION_4_BITS = UUID_VERSION_RANDOM << 4;
/// Mask preserving the random bits below the version field.
constexpr uint8 VERSION_PAYLOAD_MASK = 0x0F;
/// Mask preserving the random bits below the variant field.
constexpr uint8 VARIANT_PAYLOAD_MASK = static_cast<uint8>(~UUID_VARIANT_MASK);
/// Return value of DecodeHexDigit for a non-hexadecimal character.
constexpr uint8 INVALID_HEX_DIGIT = 0xFF;

[[nodiscard]] constexpr uint8 DecodeHexDigit(char character) noexcept
{
    if (character >= '0' && character <= '9')
    {
        return static_cast<uint8>(character - '0');
    }
    if (character >= 'a' && character <= 'f')
    {
        return static_cast<uint8>(character - 'a' + 10);
    }
    if (character >= 'A' && character <= 'F')
    {
        return static_cast<uint8>(character - 'A' + 10);
    }
    return INVALID_HEX_DIGIT;
}

[[nodiscard]] constexpr bool IsSeparatorAfterByte(usize byteIndex) noexcept
{
    for (const usize position : SEPARATOR_AFTER_BYTES)
    {
        if (position == byteIndex)
        {
            return true;
        }
    }
    return false;
}

void ApplyVersion4(Uuid& inoutValue) noexcept
{
    uint8& version = inoutValue.Bytes[UUID_VERSION_BYTE_INDEX];
    version = static_cast<uint8>((version & VERSION_PAYLOAD_MASK) | VERSION_4_BITS);
    uint8& variant = inoutValue.Bytes[UUID_VARIANT_BYTE_INDEX];
    variant = static_cast<uint8>((variant & VARIANT_PAYLOAD_MASK) | UUID_STANDARD_VARIANT_BITS);
}
} // namespace

UuidStatus ValidateUuid(const Uuid& value, UuidPolicy policy) noexcept
{
    if (value.IsNil() == true)
    {
        return policy == UuidPolicy::AllowNilOrVersion4 ? UuidStatus::Success : UuidStatus::NilIdentity;
    }
    if (value.HasStandardVariant() == false)
    {
        return UuidStatus::UnsupportedVariant;
    }
    if (value.GetVersion() != UUID_VERSION_RANDOM)
    {
        return UuidStatus::UnsupportedVersion;
    }
    return UuidStatus::Success;
}

UuidStatus TryParseUuid(const char* text, usize size, UuidPolicy policy, Uuid& outValue) noexcept
{
    if (size != UUID_TEXT_LENGTH || text == nullptr)
    {
        return UuidStatus::MalformedInput;
    }
    Uuid value;
    usize textIndex = 0;
    for (usize byteIndex = 0; byteIndex < UUID_BYTE_COUNT; ++byteIndex)
    {
        if (IsSeparatorAfterByte(byteIndex) == true)
        {
            if (text[textIndex] != '-')
            {
                return UuidStatus::MalformedInput;
            }
            ++textIndex;
        }
        const uint8 high = DecodeHexDigit(text[textIndex]);
        const uint8 low = DecodeHexDigit(text[textIndex + 1]);
        if (high == INVALID_HEX_DIGIT || low == INVALID_HEX_DIGIT)
        {
            return UuidStatus::MalformedInput;
        }
        value.Bytes[byteIndex] = static_cast<uint8>((high << 4) | low);
        textIndex += 2;
    }
    LUDUS_ASSERT(textIndex == UUID_TEXT_LENGTH);
    const UuidStatus status = ValidateUuid(value, policy);
    if (status == UuidStatus::Success)
    {
        outValue = value;
    }
    return status;
}

UuidStatus FormatUuid(const Uuid& value, char* text, usize capacity) noexcept
{
    if (capacity < UUID_TEXT_LENGTH || text == nullptr)
    {
        return UuidStatus::InsufficientSpace;
    }
    constexpr char HEX_DIGITS[] = "0123456789abcdef";
    usize textIndex = 0;
    for (usize byteIndex = 0; byteIndex < UUID_BYTE_COUNT; ++byteIndex)
    {
        if (IsSeparatorAfterByte(byteIndex) == true)
        {
            text[textIndex] = '-';
            ++textIndex;
        }
        text[textIndex] = HEX_DIGITS[value.Bytes[byteIndex] >> 4];
        text[textIndex + 1] = HEX_DIGITS[value.Bytes[byteIndex] & 0x0F];
        textIndex += 2;
    }
    LUDUS_ASSERT(textIndex == UUID_TEXT_LENGTH);
    if (capacity >= UUID_TEXT_BUFFER_BYTES)
    {
        text[UUID_TEXT_LENGTH] = '\0';
    }
    return UuidStatus::Success;
}

UuidStatus TryMakeUuidV4(const uint8* randomBytes, usize size, Uuid& outValue) noexcept
{
    if (size != UUID_BYTE_COUNT || randomBytes == nullptr)
    {
        return UuidStatus::MalformedInput;
    }
    Uuid value;
    for (usize i = 0; i < UUID_BYTE_COUNT; ++i)
    {
        value.Bytes[i] = randomBytes[i];
    }
    ApplyVersion4(value);
    outValue = value;
    return UuidStatus::Success;
}

UuidStatus TryGenerateUuidV4(const EntropySource& source, Uuid& outValue) noexcept
{
    if (source.FillBytesOrNull == nullptr)
    {
        return UuidStatus::EntropyUnavailable;
    }
    Uuid value;
    if (source.FillBytesOrNull(source.ContextOrNull, value.Bytes, UUID_BYTE_COUNT) == false)
    {
        return UuidStatus::EntropyUnavailable;
    }
    ApplyVersion4(value);
    outValue = value;
    return UuidStatus::Success;
}
} // namespace ludus::foundation::identity
