#include "python_compat.h"

#include <ludus/foundation/math/scalar.hpp>
#include <ludus/foundation/parsing/json.hpp>

#include <bit>

namespace ludus::content::scene_cook::detail
{
namespace
{
[[nodiscard]] bool IsAsciiDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

[[nodiscard]] std::string_view StripPySpace(std::string_view text) noexcept
{
    // Tokens are ASCII-trimmed; Unicode whitespace inside a decoded string literal
    // is outside the reference profile and is reported as a syntax failure.
    while (!text.empty() && IsPySpace(static_cast<uint8>(text.front())))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && IsPySpace(static_cast<uint8>(text.back())))
    {
        text.remove_suffix(1);
    }
    return text;
}

// digit (["_"] digit)*; appends the digits without underscores.
[[nodiscard]] bool TakeDigits(std::string_view& text, Sink& digits, usize& count) noexcept
{
    count = 0;
    if (text.empty() || !IsAsciiDigit(text.front()))
    {
        return true;
    }
    while (!text.empty())
    {
        if (IsAsciiDigit(text.front()))
        {
            digits.Append(text.front());
            ++count;
            text.remove_prefix(1);
        }
        else if (text.front() == '_' && text.size() > 1 && IsAsciiDigit(text[1]))
        {
            text.remove_prefix(1);
        }
        else
        {
            break;
        }
    }
    return true;
}

[[nodiscard]] bool EqualsIgnoreCase(std::string_view text, std::string_view lower) noexcept
{
    if (text.size() != lower.size())
    {
        return false;
    }
    for (usize i = 0; i < text.size(); ++i)
    {
        char value = text[i];
        if (value >= 'A' && value <= 'Z')
        {
            value = static_cast<char>(value - 'A' + 'a');
        }
        if (value != lower[i])
        {
            return false;
        }
    }
    return true;
}

enum class FloatSyntax : uint8
{
    Number,
    Nonfinite,
    Invalid,
};

// Rewrite Python float() syntax as a JSON real literal.
[[nodiscard]] FloatSyntax NormaliseFloat(std::string_view text, Sink& json) noexcept
{
    text = StripPySpace(text);
    bool negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-'))
    {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (EqualsIgnoreCase(text, "inf") || EqualsIgnoreCase(text, "infinity") || EqualsIgnoreCase(text, "nan"))
    {
        return FloatSyntax::Nonfinite;
    }
    Array<uint8> integerBytes;
    Array<uint8> fractionBytes;
    Array<uint8> exponentBytes;
    Sink integer(integerBytes);
    Sink fraction(fractionBytes);
    Sink exponent(exponentBytes);
    usize integerCount = 0;
    usize fractionCount = 0;
    (void)TakeDigits(text, integer, integerCount);
    if (!text.empty() && text.front() == '.')
    {
        text.remove_prefix(1);
        (void)TakeDigits(text, fraction, fractionCount);
    }
    if (integerCount + fractionCount == 0)
    {
        return FloatSyntax::Invalid;
    }
    bool exponentNegative = false;
    usize exponentCount = 0;
    if (!text.empty() && (text.front() == 'e' || text.front() == 'E'))
    {
        text.remove_prefix(1);
        if (!text.empty() && (text.front() == '+' || text.front() == '-'))
        {
            exponentNegative = text.front() == '-';
            text.remove_prefix(1);
        }
        (void)TakeDigits(text, exponent, exponentCount);
        if (exponentCount == 0)
        {
            return FloatSyntax::Invalid;
        }
    }
    if (!text.empty())
    {
        return FloatSyntax::Invalid;
    }
    if (negative)
    {
        json.Append('-');
    }
    // JSON forbids leading zeroes in the integer part; they never change the value.
    std::string_view digits(reinterpret_cast<const char*>(integerBytes.GetData()), integerBytes.GetSize());
    while (digits.size() > 1 && digits.front() == '0')
    {
        digits.remove_prefix(1);
    }
    json.Append(digits.empty() ? std::string_view("0") : digits);
    json.Append('.');
    json.Append(fractionCount == 0 ? std::string_view("0")
                                   : std::string_view(reinterpret_cast<const char*>(fractionBytes.GetData()),
                                                      fractionBytes.GetSize()));
    if (exponentCount != 0)
    {
        json.Append(exponentNegative ? std::string_view("e-") : std::string_view("e+"));
        json.Append(std::string_view(reinterpret_cast<const char*>(exponentBytes.GetData()), exponentBytes.GetSize()));
    }
    return integer.Ok() && fraction.Ok() && exponent.Ok() ? FloatSyntax::Number : FloatSyntax::Invalid;
}

void AppendCodeUnitEscape(Sink& sink, uint32 unit) noexcept
{
    constexpr char HEX[] = "0123456789abcdef";
    char text[6] = {'\\', 'u', HEX[(unit >> 12) & 15], HEX[(unit >> 8) & 15], HEX[(unit >> 4) & 15], HEX[unit & 15]};
    sink.Append(std::string_view(text, sizeof(text)));
}
} // namespace

void Sink::Append(std::string_view text) noexcept
{
    Bytes({reinterpret_cast<const uint8*>(text.data()), text.size()});
}

void Sink::Bytes(std::span<const uint8> data) noexcept
{
    if (mOk && !data.empty() && !mOutput.TryAddRange(data))
    {
        mOk = false;
    }
}

void Sink::U32(uint32 value) noexcept
{
    const uint8 bytes[4] = {static_cast<uint8>(value),
                            static_cast<uint8>(value >> 8),
                            static_cast<uint8>(value >> 16),
                            static_cast<uint8>(value >> 24)};
    Bytes(bytes);
}

void Sink::F32(float64 value) noexcept
{
    const auto narrow = static_cast<float32>(value);
    const uint32 bits = std::bit_cast<uint32>(narrow);
    if ((bits & 0x7F800000U) == 0x7F800000U &&
        (std::bit_cast<uint64>(value) & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL)
    {
        mOverflow = true;
        mOk = false;
        return;
    }
    U32(bits);
}

void AppendInteger(Sink& sink, int64 value) noexcept
{
    char text[24]{};
    usize at = sizeof(text);
    uint64 magnitude = value < 0 ? uint64{0} - static_cast<uint64>(value) : static_cast<uint64>(value);
    do
    {
        text[--at] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    if (value < 0)
    {
        text[--at] = '-';
    }
    sink.Append(std::string_view(text + at, sizeof(text) - at));
}

void AppendFloatRepr(Sink& sink, float64 value) noexcept
{
    const bool negative = (std::bit_cast<uint64>(value) >> 63) != 0;
    if (value == 0.0)
    {
        sink.Append(negative ? std::string_view("-0.0") : std::string_view("0.0"));
        return;
    }
    // yyjson emits the shortest round-trip digits; reformat them as CPython repr.
    uint8 buffer[64]{};
    parsing::JsonWriter writer(buffer);
    writer.Number(value);
    std::span<const uint8> written;
    if (writer.Finish(written) != parsing::ParseStatus::Ok)
    {
        sink.Append(std::string_view("nan")); // Unreachable for finite values; callers validate.
        return;
    }
    std::string_view text(reinterpret_cast<const char*>(written.data()), written.size());
    while (!text.empty() && text.back() == '\n')
    {
        text.remove_suffix(1);
    }
    if (!text.empty() && text.front() == '-')
    {
        text.remove_prefix(1);
    }
    char digits[32]{};
    usize count = 0;
    int64 pointAt = -1;
    int64 exponent = 0;
    usize index = 0;
    for (; index < text.size() && text[index] != 'e' && text[index] != 'E'; ++index)
    {
        if (text[index] == '.')
        {
            pointAt = static_cast<int64>(count);
        }
        else if (count < sizeof(digits))
        {
            digits[count++] = text[index];
        }
    }
    if (pointAt < 0)
    {
        pointAt = static_cast<int64>(count);
    }
    if (index < text.size())
    {
        ++index;
        bool exponentNegative = false;
        if (index < text.size() && (text[index] == '+' || text[index] == '-'))
        {
            exponentNegative = text[index] == '-';
            ++index;
        }
        for (; index < text.size(); ++index)
        {
            exponent = exponent * 10 + (text[index] - '0');
        }
        if (exponentNegative)
        {
            exponent = -exponent;
        }
    }
    // Strip leading and trailing zero digits, tracking the decimal point.
    usize first = 0;
    while (first + 1 < count && digits[first] == '0')
    {
        ++first;
        --pointAt;
    }
    while (count > first + 1 && digits[count - 1] == '0')
    {
        --count;
    }
    const std::string_view significant(digits + first, count - first);
    const int64 decpt = pointAt + exponent; // value = 0.d1d2... * 10^decpt
    const auto n = static_cast<int64>(significant.size());
    if (negative)
    {
        sink.Append('-');
    }
    if (decpt > -4 && decpt <= 16)
    {
        if (decpt <= 0)
        {
            sink.Append("0.");
            for (int64 i = 0; i < -decpt; ++i)
            {
                sink.Append('0');
            }
            sink.Append(significant);
        }
        else if (decpt >= n)
        {
            sink.Append(significant);
            for (int64 i = n; i < decpt; ++i)
            {
                sink.Append('0');
            }
            sink.Append(".0");
        }
        else
        {
            sink.Append(significant.substr(0, static_cast<usize>(decpt)));
            sink.Append('.');
            sink.Append(significant.substr(static_cast<usize>(decpt)));
        }
        return;
    }
    sink.Append(significant.substr(0, 1));
    if (n > 1)
    {
        sink.Append('.');
        sink.Append(significant.substr(1));
    }
    const int64 shown = decpt - 1;
    sink.Append(shown < 0 ? std::string_view("e-") : std::string_view("e+"));
    const int64 magnitude = shown < 0 ? -shown : shown;
    if (magnitude < 10)
    {
        sink.Append('0');
    }
    AppendInteger(sink, magnitude);
}

void AppendJsonString(Sink& sink, std::string_view value) noexcept
{
    sink.Append('"');
    usize i = 0;
    while (i < value.size())
    {
        const auto lead = static_cast<uint8>(value[i]);
        uint32 code = lead;
        usize length = 1;
        if (lead >= 0xF0 && i + 3 < value.size())
        {
            code = ((lead & 0x07U) << 18) | ((static_cast<uint8>(value[i + 1]) & 0x3FU) << 12) |
                   ((static_cast<uint8>(value[i + 2]) & 0x3FU) << 6) | (static_cast<uint8>(value[i + 3]) & 0x3FU);
            length = 4;
        }
        else if (lead >= 0xE0 && i + 2 < value.size())
        {
            code = ((lead & 0x0FU) << 12) | ((static_cast<uint8>(value[i + 1]) & 0x3FU) << 6) |
                   (static_cast<uint8>(value[i + 2]) & 0x3FU);
            length = 3;
        }
        else if (lead >= 0xC0 && i + 1 < value.size())
        {
            code = ((lead & 0x1FU) << 6) | (static_cast<uint8>(value[i + 1]) & 0x3FU);
            length = 2;
        }
        i += length;
        switch (code)
        {
            case '"':
                sink.Append("\\\"");
                break;
            case '\\':
                sink.Append("\\\\");
                break;
            case '\n':
                sink.Append("\\n");
                break;
            case '\r':
                sink.Append("\\r");
                break;
            case '\t':
                sink.Append("\\t");
                break;
            case '\b':
                sink.Append("\\b");
                break;
            case '\f':
                sink.Append("\\f");
                break;
            default:
                if (code >= 0x20 && code < 0x7F)
                {
                    sink.Append(static_cast<char>(code));
                }
                else if (code >= 0x10000)
                {
                    const uint32 offset = code - 0x10000;
                    AppendCodeUnitEscape(sink, 0xD800 | (offset >> 10));
                    AppendCodeUnitEscape(sink, 0xDC00 | (offset & 0x3FF));
                }
                else
                {
                    AppendCodeUnitEscape(sink, code);
                }
                break;
        }
    }
    sink.Append('"');
}

void AppendHex(Sink& sink, std::span<const uint8> digest) noexcept
{
    constexpr char HEX[] = "0123456789abcdef";
    for (const uint8 value : digest)
    {
        const char pair[2] = {HEX[value >> 4], HEX[value & 15]};
        sink.Append(std::string_view(pair, 2));
    }
}

void PyJson::Newline() noexcept
{
    mSink.Append('\n');
    for (usize i = 0; i < mDepth; ++i)
    {
        mSink.Append("  ");
    }
}

void PyJson::BeforeValue() noexcept
{
    if (mAfterKey)
    {
        mAfterKey = false;
        return;
    }
    if (mDepth == 0)
    {
        return;
    }
    if (!mFirst)
    {
        mSink.Append(',');
    }
    Newline();
}

void PyJson::BeginObject() noexcept
{
    BeforeValue();
    mSink.Append('{');
    ++mDepth;
    mFirst = true;
}

void PyJson::EndObject() noexcept
{
    --mDepth;
    if (!mFirst)
    {
        Newline();
    }
    mSink.Append('}');
    mFirst = false;
}

void PyJson::BeginArray() noexcept
{
    BeforeValue();
    mSink.Append('[');
    ++mDepth;
    mFirst = true;
}

void PyJson::EndArray() noexcept
{
    --mDepth;
    if (!mFirst)
    {
        Newline();
    }
    mSink.Append(']');
    mFirst = false;
}

void PyJson::Key(std::string_view key) noexcept
{
    if (!mFirst)
    {
        mSink.Append(',');
    }
    Newline();
    AppendJsonString(mSink, key);
    mSink.Append(": ");
    mAfterKey = true;
}

void PyJson::String(std::string_view value) noexcept
{
    BeforeValue();
    AppendJsonString(mSink, value);
    mFirst = false;
}

void PyJson::Integer(int64 value) noexcept
{
    BeforeValue();
    AppendInteger(mSink, value);
    mFirst = false;
}

void PyJson::Float(float64 value) noexcept
{
    BeforeValue();
    AppendFloatRepr(mSink, value);
    mFirst = false;
}

void PyJson::Boolean(bool value) noexcept
{
    BeforeValue();
    mSink.Append(value ? std::string_view("true") : std::string_view("false"));
    mFirst = false;
}

void PyJson::Null() noexcept
{
    BeforeValue();
    mSink.Append("null");
    mFirst = false;
}

void PySum::Add(float64 value) noexcept
{
    const float64 total = Total + value;
    const float64 absTotal = Total < 0.0 ? -Total : Total;
    const float64 absValue = value < 0.0 ? -value : value;
    if (absTotal >= absValue)
    {
        Compensation += (Total - total) + value;
    }
    else
    {
        Compensation += (value - total) + Total;
    }
    Total = total;
}

float64 PySum::Result() const noexcept
{
    const bool finite = (std::bit_cast<uint64>(Compensation) & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL;
    return Compensation != 0.0 && finite ? Total + Compensation : Total;
}

NumberResult ParseFloats(std::span<const std::string_view> tokens, Array<float64>& output) noexcept
{
    output.Clear();
    if (tokens.empty())
    {
        return NumberResult::Ok;
    }
    Array<uint8> jsonBytes;
    Sink json(jsonBytes);
    json.Append('[');
    bool nonfinite = false;
    for (usize i = 0; i < tokens.size(); ++i)
    {
        if (i != 0)
        {
            json.Append(',');
        }
        const FloatSyntax syntax = NormaliseFloat(tokens[i], json);
        if (syntax == FloatSyntax::Invalid)
        {
            return NumberResult::Syntax;
        }
        if (syntax == FloatSyntax::Nonfinite)
        {
            nonfinite = true;
            json.Append('0');
        }
    }
    json.Append(']');
    if (!json.Ok())
    {
        return NumberResult::OutOfMemory;
    }
    if (nonfinite)
    {
        return NumberResult::Nonfinite;
    }
    parsing::JsonLimits limits;
    limits.MaxInputBytes = jsonBytes.GetSize();
    limits.MaxWorkspaceBytes = jsonBytes.GetSize() * 32 + 4096;
    limits.MaxArrayElements = tokens.size();
    limits.MaxValues = tokens.size() + 1;
    parsing::JsonDocument document;
    parsing::ParseError parseError{};
    const auto status =
        document.Read(std::string_view(reinterpret_cast<const char*>(jsonBytes.GetData()), jsonBytes.GetSize()),
                      parseError,
                      limits);
    if (status == parsing::ParseStatus::OutOfMemory)
    {
        return NumberResult::OutOfMemory;
    }
    if (status != parsing::ParseStatus::Ok)
    {
        // The syntax is valid by construction; yyjson rejects only overflow to infinity.
        return NumberResult::Nonfinite;
    }
    const parsing::JsonValue root = document.Root();
    if (!output.TryResize(tokens.size()))
    {
        return NumberResult::OutOfMemory;
    }
    for (usize i = 0; i < tokens.size(); ++i)
    {
        float64 value = 0.0;
        if (!root.At(i).Number(value))
        {
            return NumberResult::Syntax;
        }
        if ((std::bit_cast<uint64>(value) & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL)
        {
            return NumberResult::Nonfinite;
        }
        output[i] = value;
    }
    return NumberResult::Ok;
}

bool ParseInt(std::string_view text, int64& output) noexcept
{
    text = StripPySpace(text);
    bool negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-'))
    {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (text.empty() || !IsAsciiDigit(text.front()))
    {
        return false;
    }
    uint64 value = 0;
    while (!text.empty())
    {
        if (IsAsciiDigit(text.front()))
        {
            const auto digit = static_cast<uint64>(text.front() - '0');
            if (value > (uint64{1} << 62) / 5) // Beyond int64 magnitude after * 10.
            {
                return false;
            }
            value = value * 10 + digit;
            text.remove_prefix(1);
        }
        else if (text.front() == '_' && text.size() > 1 && IsAsciiDigit(text[1]))
        {
            text.remove_prefix(1);
        }
        else
        {
            return false;
        }
    }
    if (value > (uint64{1} << 63) - 1)
    {
        return false;
    }
    output = negative ? -static_cast<int64>(value) : static_cast<int64>(value);
    return true;
}

bool DecodeJsonString(std::string_view token, foundation::String& output) noexcept
{
    parsing::JsonDocument document;
    parsing::ParseError parseError{};
    parsing::JsonLimits limits;
    limits.MaxInputBytes = token.size() + 1;
    if (document.Read(token, parseError, limits) != parsing::ParseStatus::Ok)
    {
        return false;
    }
    std::string_view value;
    if (!document.Root().String(value))
    {
        return false;
    }
    return output.TryAssign(value) == StringStatus::Ok;
}

bool IsPySpace(uint32 codePoint) noexcept
{
    switch (codePoint)
    {
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C:
        case 0x0D:
        case 0x1C:
        case 0x1D:
        case 0x1E:
        case 0x1F:
        case 0x20:
        case 0x85:
        case 0xA0:
        case 0x1680:
        case 0x2028:
        case 0x2029:
        case 0x202F:
        case 0x205F:
        case 0x3000:
            return true;
        default:
            return codePoint >= 0x2000 && codePoint <= 0x200A;
    }
}

float64 RoundHalfEven(float64 value) noexcept
{
    const float64 lower = math::Floor(value);
    const float64 difference = value - lower;
    if (difference > 0.5)
    {
        return lower + 1.0;
    }
    if (difference < 0.5)
    {
        return lower;
    }
    const float64 half = lower * 0.5;
    return math::Floor(half) == half ? lower : lower + 1.0;
}
} // namespace ludus::content::scene_cook::detail
