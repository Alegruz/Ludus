#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::foundation::diagnostics::internal
{
// Diagnostic-only bounded byte builder, not a formatting/string facility.
// Reserve the tail up front so truncation can never lose its marker/terminator.
class TextWriter
{
public:
    TextWriter(char* data, usize capacity) noexcept : mData(data), mLimit(capacity - sizeof(" [truncated]\n")) {}

    void Byte(char byte) noexcept
    {
        if (mSize < mLimit)
        {
            mData[mSize++] = byte;
        }
        else
        {
            mTruncated = true;
        }
    }

    void Raw(const char* text) noexcept
    {
        for (usize i = 0; text[i] != '\0'; ++i)
        {
            Byte(text[i]);
        }
    }

    void Text(const char* text, usize quota = 2048) noexcept
    {
        if (text == nullptr)
        {
            Raw("<none>");
            return;
        }
        const usize start = mSize;
        constexpr char hex[] = "0123456789abcdef";
        for (usize i = 0; i < 1024; ++i)
        {
            if (mSize == mLimit)
            {
                mTruncated = true;
                return;
            }
            const auto byte = static_cast<uint8>(text[i]);
            if (byte == 0)
            {
                return;
            }
            const usize encoded_size = byte < 32 || byte == 127 || byte == '\\' ? 4 : 1;
            if (encoded_size > quota - (mSize - start) || encoded_size > mLimit - mSize)
            {
                mTruncated = true;
                return;
            }
            if (byte < 32 || byte == 127 || byte == '\\')
            {
                Raw("\\x");
                Byte(hex[byte >> 4]);
                Byte(hex[byte & 15]);
            }
            else
            {
                Byte(static_cast<char>(byte));
            }
        }
        mTruncated = true; // Never read a 1025th byte to discover the terminator.
    }

    // Only internally rendered bytes, never borrowed user text. Preserve complete
    // control escapes when the enclosing report has less remaining space.
    void Rendered(const char* data, usize size) noexcept
    {
        for (usize i = 0; i < size;)
        {
            const usize count = data[i] == '\\' && size - i >= 4 ? 4 : 1;
            if (count > mLimit - mSize)
            {
                mTruncated = true;
                return;
            }
            for (usize n = 0; n < count; ++n)
            {
                Byte(data[i++]);
            }
        }
    }

    void Truncate() noexcept
    {
        mTruncated = true;
    }

    void EscapedByte(char value) noexcept
    {
        constexpr char hex[] = "0123456789abcdef";
        const auto byte = static_cast<uint8>(value);
        const usize required = byte < 32 || byte == 127 || byte == '\\' ? 4 : 1;
        if (required > mLimit - mSize)
        {
            mTruncated = true;
            return;
        }
        if (required == 4)
        {
            Raw("\\x");
            Byte(hex[byte >> 4]);
            Byte(hex[byte & 15]);
        }
        else
        {
            Byte(value);
        }
    }

    void Counted(const char* data, usize size) noexcept
    {
        if (data == nullptr)
        {
            Raw(size == 0 ? "<null>" : "[invalid-text]");
            return;
        }
        const usize clipped = size < 1024 ? size : 1024;
        for (usize i = 0; i < clipped; ++i)
        {
            EscapedByte(data[i]);
        }
        if (clipped != size)
        {
            mTruncated = true;
        }
    }

    void Number(uint64 value) noexcept
    {
        char digits[20];
        usize count = 0;
        do
        {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (count != 0)
        {
            Byte(digits[--count]);
        }
    }

    usize Finish() noexcept
    {
        if (mTruncated)
        {
            constexpr char marker[] = " [truncated]";
            for (usize i = 0; i < sizeof(marker) - 1; ++i)
            {
                mData[mSize++] = marker[i];
            }
        }
        mData[mSize++] = '\n';
        mData[mSize] = '\0';
        return mSize;
    }

private:
    char* mData;
    usize mLimit;
    usize mSize = 0;
    bool mTruncated = false;
};

} // namespace ludus::foundation::diagnostics::internal
