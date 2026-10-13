#pragma once

// Sequential byte input for the streaming pbrt tokenizer.

#include <ludus/content/content.h>
#include <ludus/foundation/base/core.h>

#include <span>

namespace ludus::content::scene_cook::importers
{
using foundation::core::uint8;
using foundation::core::usize;

/// Pull-style byte stream; Read fills a prefix of the buffer.
class ByteSource
{
public:
    ByteSource() noexcept = default;
    ByteSource(const ByteSource&) = delete;
    ByteSource& operator=(const ByteSource&) = delete;
    virtual ~ByteSource() noexcept = default;
    /// Read up to outBuffer.size() bytes; outCount is zero at end of input.
    /// @return Ok, or IoError when the underlying read failed.
    [[nodiscard]] virtual Status Read(std::span<uint8> outBuffer, usize& outCount) noexcept = 0;
};

/// ByteSource over caller-owned memory that must outlive the source.
class MemoryByteSource final : public ByteSource
{
public:
    explicit MemoryByteSource(std::span<const uint8> data) noexcept : mData(data) {}
    [[nodiscard]] Status Read(std::span<uint8> outBuffer, usize& outCount) noexcept override
    {
        const usize remaining = mData.size() - mOffset;
        outCount = remaining < outBuffer.size() ? remaining : outBuffer.size();
        for (usize i = 0; i < outCount; ++i)
        {
            outBuffer[i] = mData[mOffset + i];
        }
        mOffset += outCount;
        return Status::Ok;
    }

private:
    std::span<const uint8> mData;
    usize mOffset = 0;
};
} // namespace ludus::content::scene_cook::importers
