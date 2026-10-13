#include "uuid.h"

#include <unistd.h>
#if defined(__APPLE__)
#    include <sys/random.h>
#endif

namespace ludus::content::scene_cook::detail
{
namespace
{
[[nodiscard]] uint32 RotateLeft(uint32 value, uint32 count) noexcept
{
    return (value << count) | (value >> (32 - count));
}

void ProcessSha1Block(const uint8* block, uint32 (&inoutState)[5]) noexcept
{
    uint32 words[80]{};
    for (usize i = 0; i < 16; ++i)
    {
        words[i] = (static_cast<uint32>(block[i * 4]) << 24) | (static_cast<uint32>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32>(block[i * 4 + 2]) << 8) | static_cast<uint32>(block[i * 4 + 3]);
    }
    for (usize i = 16; i < 80; ++i)
    {
        words[i] = RotateLeft(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
    }
    uint32 a = inoutState[0];
    uint32 b = inoutState[1];
    uint32 c = inoutState[2];
    uint32 d = inoutState[3];
    uint32 e = inoutState[4];
    for (usize i = 0; i < 80; ++i)
    {
        uint32 mix = 0;
        uint32 constant = 0;
        if (i < 20)
        {
            mix = (b & c) | ((~b) & d);
            constant = 0x5a827999;
        }
        else if (i < 40)
        {
            mix = b ^ c ^ d;
            constant = 0x6ed9eba1;
        }
        else if (i < 60)
        {
            mix = (b & c) | (b & d) | (c & d);
            constant = 0x8f1bbcdc;
        }
        else
        {
            mix = b ^ c ^ d;
            constant = 0xca62c1d6;
        }
        const uint32 next = RotateLeft(a, 5) + mix + e + constant + words[i];
        e = d;
        d = c;
        c = RotateLeft(b, 30);
        b = a;
        a = next;
    }
    inoutState[0] += a;
    inoutState[1] += b;
    inoutState[2] += c;
    inoutState[3] += d;
    inoutState[4] += e;
}

[[nodiscard]] int GetHexDigit(char value) noexcept
{
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f')
    {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F')
    {
        return value - 'A' + 10;
    }
    return -1;
}

/// Python str.replace(old, "") into a bounded buffer; false when it would overflow.
[[nodiscard]] usize RemoveAll(char* inoutText, usize size, std::string_view pattern) noexcept
{
    usize write = 0;
    usize read = 0;
    while (read < size)
    {
        if (read + pattern.size() <= size && std::string_view(inoutText + read, pattern.size()) == pattern)
        {
            read += pattern.size();
            continue;
        }
        inoutText[write++] = inoutText[read++];
    }
    return write;
}
} // namespace

void ComputeSha1(std::span<const uint8> data, uint8 (&outDigest)[20]) noexcept
{
    uint32 state[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    usize offset = 0;
    while (data.size() - offset >= 64)
    {
        ProcessSha1Block(data.data() + offset, state);
        offset += 64;
    }
    uint8 tail[128]{};
    const usize remainder = data.size() - offset;
    for (usize i = 0; i < remainder; ++i)
    {
        tail[i] = data[offset + i];
    }
    tail[remainder] = 0x80;
    const usize tailSize = remainder + 9 <= 64 ? 64 : 128;
    const uint64 bits = static_cast<uint64>(data.size()) * 8;
    for (usize i = 0; i < 8; ++i)
    {
        tail[tailSize - 1 - i] = static_cast<uint8>(bits >> (i * 8));
    }
    ProcessSha1Block(tail, state);
    if (tailSize == 128)
    {
        ProcessSha1Block(tail + 64, state);
    }
    for (usize i = 0; i < 5; ++i)
    {
        outDigest[i * 4] = static_cast<uint8>(state[i] >> 24);
        outDigest[i * 4 + 1] = static_cast<uint8>(state[i] >> 16);
        outDigest[i * 4 + 2] = static_cast<uint8>(state[i] >> 8);
        outDigest[i * 4 + 3] = static_cast<uint8>(state[i]);
    }
}

void AppendUuid(ByteWriter& inoutWriter, const Uuid& value) noexcept
{
    static constexpr char HEX[] = "0123456789abcdef";
    char text[UUID_TEXT_BYTES]{};
    usize at = 0;
    for (usize i = 0; i < UUID_BYTES; ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10)
        {
            text[at++] = '-';
        }
        text[at++] = HEX[value.Bytes[i] >> 4];
        text[at++] = HEX[value.Bytes[i] & 0xf];
    }
    inoutWriter.Append(std::string_view(text, UUID_TEXT_BYTES));
}

bool ParseUuid(std::string_view text, Uuid& outValue) noexcept
{
    // hex.replace('urn:', '').replace('uuid:', ''); hex.strip('{}').replace('-', '');
    // then len(hex) == 32 and int(hex, 16). Longer inputs cannot shrink to 32 digits
    // plausibly; bound the work to a small buffer.
    constexpr usize MAX_INPUT_BYTES = 256;
    if (text.size() > MAX_INPUT_BYTES)
    {
        return false;
    }
    char buffer[MAX_INPUT_BYTES]{};
    for (usize i = 0; i < text.size(); ++i)
    {
        buffer[i] = text[i];
    }
    usize size = RemoveAll(buffer, text.size(), "urn:");
    size = RemoveAll(buffer, size, "uuid:");
    usize first = 0;
    while (first < size && (buffer[first] == '{' || buffer[first] == '}'))
    {
        ++first;
    }
    while (size > first && (buffer[size - 1] == '{' || buffer[size - 1] == '}'))
    {
        --size;
    }
    size = first + RemoveAll(buffer + first, size - first, "-");
    std::string_view digits(buffer + first, size - first);
    if (digits.size() != 32)
    {
        return false;
    }
    // int(digits, 16): ASCII whitespace, an optional '+' sign, an optional 0x prefix and
    // single underscores between digits are accepted; a negative value fails the range check.
    while (digits.empty() == false && (digits.front() == ' ' || (digits.front() >= '\t' && digits.front() <= '\r')))
    {
        digits.remove_prefix(1);
    }
    while (digits.empty() == false && (digits.back() == ' ' || (digits.back() >= '\t' && digits.back() <= '\r')))
    {
        digits.remove_suffix(1);
    }
    bool isNegative = false;
    if (digits.empty() == false && (digits.front() == '+' || digits.front() == '-'))
    {
        isNegative = digits.front() == '-';
        digits.remove_prefix(1);
    }
    bool hasPrefix = false;
    if (digits.size() >= 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
    {
        digits.remove_prefix(2);
        hasPrefix = true;
    }
    Uuid value;
    bool hasDigit = false;
    bool wasUnderscore = hasPrefix;
    bool isZero = true;
    for (const char item : digits)
    {
        if (item == '_')
        {
            if (wasUnderscore == true && hasDigit == true)
            {
                return false;
            }
            if (hasDigit == false && hasPrefix == false)
            {
                return false;
            }
            wasUnderscore = true;
            continue;
        }
        const int digit = GetHexDigit(item);
        if (digit < 0)
        {
            return false;
        }
        // Shift the 128-bit big-endian value left by four bits.
        for (usize i = 0; i < UUID_BYTES; ++i)
        {
            const uint8 carry = i + 1 < UUID_BYTES ? static_cast<uint8>(value.Bytes[i + 1] >> 4) : 0;
            value.Bytes[i] = static_cast<uint8>((value.Bytes[i] << 4) | carry);
        }
        value.Bytes[UUID_BYTES - 1] = static_cast<uint8>(value.Bytes[UUID_BYTES - 1] | digit);
        isZero = isZero && digit == 0;
        hasDigit = true;
        wasUnderscore = false;
    }
    if (hasDigit == false || wasUnderscore == true || (isNegative == true && isZero == false))
    {
        return false;
    }
    outValue = value;
    return true;
}

bool MakeNameUuid(const Uuid& space, std::string_view name, Uuid& outValue) noexcept
{
    constexpr usize MAX_NAME_BYTES = 240;
    if (name.size() > MAX_NAME_BYTES)
    {
        return false;
    }
    uint8 data[UUID_BYTES + MAX_NAME_BYTES]{};
    for (usize i = 0; i < UUID_BYTES; ++i)
    {
        data[i] = space.Bytes[i];
    }
    for (usize i = 0; i < name.size(); ++i)
    {
        data[UUID_BYTES + i] = static_cast<uint8>(name[i]);
    }
    uint8 digest[20]{};
    ComputeSha1(std::span<const uint8>(data, UUID_BYTES + name.size()), digest);
    Uuid value;
    for (usize i = 0; i < UUID_BYTES; ++i)
    {
        value.Bytes[i] = digest[i];
    }
    value.Bytes[6] = static_cast<uint8>((value.Bytes[6] & 0x0f) | 0x50);
    value.Bytes[8] = static_cast<uint8>((value.Bytes[8] & 0x3f) | 0x80);
    outValue = value;
    return true;
}

bool MakeRandomUuid(Uuid& outValue) noexcept
{
    Uuid value;
    if (getentropy(value.Bytes, UUID_BYTES) != 0)
    {
        return false;
    }
    value.Bytes[6] = static_cast<uint8>((value.Bytes[6] & 0x0f) | 0x40);
    value.Bytes[8] = static_cast<uint8>((value.Bytes[8] & 0x3f) | 0x80);
    outValue = value;
    return true;
}
} // namespace ludus::content::scene_cook::detail
