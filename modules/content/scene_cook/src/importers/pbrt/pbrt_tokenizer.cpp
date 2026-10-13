#include "pbrt_tokenizer.h"

#include <ludus/foundation/base/parse_number.hpp>
#include <ludus/foundation/math/scalar.hpp>

namespace ludus::content::scene_cook::importers::pbrt
{
namespace
{
[[nodiscard]] bool IsSpace(uint8 value) noexcept
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' || value == '\v';
}

[[nodiscard]] bool IsDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

[[nodiscard]] bool IsNumberStart(uint8 value) noexcept
{
    return IsDigit(static_cast<char>(value)) == true || value == '-' || value == '+' || value == '.';
}

[[nodiscard]] bool IsWordByte(uint8 value) noexcept
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || IsDigit(static_cast<char>(value)) ||
           value == '_';
}

[[nodiscard]] bool IsBareByte(uint8 value) noexcept
{
    return IsSpace(value) == false && value != '"' && value != '[' && value != ']' && value != '#';
}
} // namespace

bool Tokenizer::peekByte(uint8& outByte) noexcept
{
    if (mBufferOffset == mBufferSize)
    {
        if (mIsAtEnd == true)
        {
            return false;
        }
        usize count = 0;
        if (mSource.Read(std::span<uint8>(mBuffer, BUFFER_BYTES), count) != Status::Ok)
        {
            mHasReadFailed = true;
            mIsAtEnd = true;
            return false;
        }
        mBufferOffset = 0;
        mBufferSize = count;
        if (count == 0)
        {
            mIsAtEnd = true;
            return false;
        }
    }
    outByte = mBuffer[mBufferOffset];
    return true;
}

void Tokenizer::consumeByte() noexcept
{
    if (mBuffer[mBufferOffset] == '\n')
    {
        ++mLine;
        mColumn = 1;
    }
    else
    {
        ++mColumn;
    }
    ++mBufferOffset;
}

TokenizerStatus Tokenizer::failSyntax(std::string_view message) noexcept
{
    mErrorMessage = message;
    mErrorLine = mCurrent.Line;
    return TokenizerStatus::SyntaxError;
}

TokenizerStatus Tokenizer::appendToToken(char value) noexcept
{
    if (mCurrent.Text.GetSize() >= MAX_TOKEN_BYTES)
    {
        return failSyntax("token exceeds the length limit");
    }
    return mCurrent.Text.TryAppend(std::string_view(&value, 1)) == foundation::StringStatus::Ok
               ? TokenizerStatus::Ok
               : TokenizerStatus::OutOfMemory;
}

TokenizerStatus Tokenizer::readString() noexcept
{
    consumeByte(); // Opening quote.
    uint8 value = 0;
    while (true)
    {
        if (peekByte(value) == false)
        {
            return mHasReadFailed == true ? TokenizerStatus::IoError : failSyntax("unterminated string");
        }
        if (value == '\n')
        {
            return failSyntax("newline in string");
        }
        consumeByte();
        if (value == '"')
        {
            return TokenizerStatus::Ok;
        }
        char decoded = static_cast<char>(value);
        if (value == '\\')
        {
            if (peekByte(value) == false)
            {
                return mHasReadFailed == true ? TokenizerStatus::IoError : failSyntax("unterminated string");
            }
            consumeByte();
            switch (value)
            {
                case 'n':
                    decoded = '\n';
                    break;
                case 't':
                    decoded = '\t';
                    break;
                case 'r':
                    decoded = '\r';
                    break;
                case 'b':
                    decoded = '\b';
                    break;
                case 'f':
                    decoded = '\f';
                    break;
                case '\\':
                case '"':
                case '\'':
                    decoded = static_cast<char>(value);
                    break;
                default:
                    return failSyntax("invalid escape in string");
            }
        }
        const TokenizerStatus status = appendToToken(decoded);
        if (status != TokenizerStatus::Ok)
        {
            return status;
        }
    }
}

TokenizerStatus Tokenizer::readBare(TokenKind kind) noexcept
{
    uint8 value = 0;
    while (peekByte(value) == true && IsBareByte(value) == true)
    {
        if (kind == TokenKind::Word && IsWordByte(value) == false)
        {
            return failSyntax("unexpected character");
        }
        consumeByte();
        const TokenizerStatus status = appendToToken(static_cast<char>(value));
        if (status != TokenizerStatus::Ok)
        {
            return status;
        }
    }
    return mHasReadFailed == true ? TokenizerStatus::IoError : TokenizerStatus::Ok;
}

TokenizerStatus Tokenizer::Advance() noexcept
{
    mCurrent.Text.Clear();
    uint8 value = 0;
    while (true)
    {
        if (peekByte(value) == false)
        {
            mCurrent.Kind = TokenKind::End;
            mCurrent.Line = mLine;
            mCurrent.Column = mColumn;
            return mHasReadFailed == true ? TokenizerStatus::IoError : TokenizerStatus::Ok;
        }
        if (IsSpace(value) == true)
        {
            consumeByte();
            continue;
        }
        if (value == '#')
        {
            while (peekByte(value) == true && value != '\n')
            {
                consumeByte();
            }
            continue;
        }
        break;
    }
    mCurrent.Line = mLine;
    mCurrent.Column = mColumn;
    if (value == '[' || value == ']')
    {
        consumeByte();
        mCurrent.Kind = value == '[' ? TokenKind::OpenBracket : TokenKind::CloseBracket;
        return TokenizerStatus::Ok;
    }
    if (value == '"')
    {
        mCurrent.Kind = TokenKind::String;
        return readString();
    }
    if (IsNumberStart(value) == true)
    {
        mCurrent.Kind = TokenKind::Number;
        return readBare(TokenKind::Number);
    }
    if (IsWordByte(value) == true)
    {
        mCurrent.Kind = TokenKind::Word;
        return readBare(TokenKind::Word);
    }
    return failSyntax("unexpected character");
}

bool ParseNumber(std::string_view text, float64& outValue) noexcept
{
    // Normalise to the JSON number grammar accepted by ParseFloat32:
    // drop '+', drop redundant leading zeros, add '0' around a bare '.'.
    char normalised[MAX_TOKEN_BYTES + 8];
    usize length = 0;
    usize at = 0;
    bool isNegative = false;
    if (at < text.size() && (text[at] == '+' || text[at] == '-'))
    {
        isNegative = text[at] == '-';
        ++at;
    }
    if (isNegative == true)
    {
        normalised[length++] = '-';
    }
    const usize integerStart = at;
    while (at < text.size() && IsDigit(text[at]) == true)
    {
        ++at;
    }
    usize integerFirst = integerStart;
    while (at - integerFirst > 1 && text[integerFirst] == '0')
    {
        ++integerFirst;
    }
    if (integerFirst == at)
    {
        normalised[length++] = '0';
    }
    for (usize i = integerFirst; i < at; ++i)
    {
        normalised[length++] = text[i];
    }
    bool hasDigits = at > integerStart;
    if (at < text.size() && text[at] == '.')
    {
        ++at;
        const usize fractionStart = at;
        while (at < text.size() && IsDigit(text[at]) == true)
        {
            ++at;
        }
        hasDigits = hasDigits == true || at > fractionStart;
        if (at > fractionStart)
        {
            normalised[length++] = '.';
            for (usize i = fractionStart; i < at; ++i)
            {
                normalised[length++] = text[i];
            }
        }
    }
    if (hasDigits == false || text.size() > MAX_TOKEN_BYTES)
    {
        return false;
    }
    if (at < text.size() && (text[at] == 'e' || text[at] == 'E'))
    {
        normalised[length++] = 'e';
        ++at;
        if (at < text.size() && (text[at] == '+' || text[at] == '-'))
        {
            normalised[length++] = text[at];
            ++at;
        }
        const usize exponentStart = at;
        while (at < text.size() && IsDigit(text[at]) == true)
        {
            normalised[length++] = text[at];
            ++at;
        }
        if (at == exponentStart)
        {
            return false;
        }
    }
    if (at != text.size())
    {
        return false;
    }
    float32 parsed = 0.0F;
    const foundation::NumberParseStatus status = foundation::ParseFloat32(normalised, length, parsed);
    if (status == foundation::NumberParseStatus::Success)
    {
        outValue = static_cast<float64>(parsed);
        return true;
    }
    if (status != foundation::NumberParseStatus::OutOfRange)
    {
        return false;
    }
    // Distinguish underflow (exponent negative) from overflow.
    for (usize i = 0; i < length; ++i)
    {
        if (normalised[i] == 'e' && i + 1 < length && normalised[i + 1] == '-')
        {
            outValue = isNegative == true ? -0.0 : 0.0;
            return true;
        }
    }
    return false;
}
} // namespace ludus::content::scene_cook::importers::pbrt
