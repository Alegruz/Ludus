#include <ludus/foundation/strings/utf8.hpp>

namespace ludus::foundation
{
// Thanks to the Unicode Consortium, The Unicode Standard 17.0, section 3.9,
// table 3-7 (https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/).
// Validate only well-formed UTF-8 scalar sequences; normalization and grapheme
// handling stay above FoundationStrings. See docs/architecture/strings.md.
StringStatus ValidateUtf8(std::string_view bytes, Utf8View& output, usize& errorOffset) noexcept
{
    usize i = 0;
    while (i < bytes.size())
    {
        const uint8 lead = static_cast<uint8>(bytes[i]);
        if (lead < 0x80)
        {
            ++i;
            continue;
        }
        usize length = 0;
        uint8 minimum = 0x80;
        uint8 maximum = 0xbf;
        if (lead >= 0xc2 && lead <= 0xdf)
        {
            length = 2;
        }
        else if (lead >= 0xe0 && lead <= 0xef)
        {
            length = 3;
            if (lead == 0xe0)
            {
                minimum = 0xa0;
            }
            if (lead == 0xed)
            {
                maximum = 0x9f;
            }
        }
        else if (lead >= 0xf0 && lead <= 0xf4)
        {
            length = 4;
            if (lead == 0xf0)
            {
                minimum = 0x90;
            }
            if (lead == 0xf4)
            {
                maximum = 0x8f;
            }
        }
        if (length == 0 || length > bytes.size() - i || static_cast<uint8>(bytes[i + 1]) < minimum ||
            static_cast<uint8>(bytes[i + 1]) > maximum)
        {
            errorOffset = i;
            return StringStatus::InvalidUtf8;
        }
        for (usize j = 2; j < length; ++j)
        {
            const uint8 continuation = static_cast<uint8>(bytes[i + j]);
            if (continuation < 0x80 || continuation > 0xbf)
            {
                errorOffset = i;
                return StringStatus::InvalidUtf8;
            }
        }
        i += length;
    }
    output.mView = bytes;
    errorOffset = 0;
    return StringStatus::Ok;
}
StringStatus ValidateCString(std::string_view bytes) noexcept
{
    return bytes.find('\0') == std::string_view::npos ? StringStatus::Ok : StringStatus::EmbeddedZero;
}
} // namespace ludus::foundation
