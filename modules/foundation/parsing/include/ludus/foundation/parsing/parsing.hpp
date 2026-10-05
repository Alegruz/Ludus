#pragma once

#include <ludus/foundation/base/types.h>

#include <span>
#include <string_view>

namespace ludus::foundation::parsing
{
enum class ParseStatus : uint8
{
    Ok,
    InvalidSyntax,
    InvalidEncoding,
    DuplicateKey,
    LimitExceeded,
    OutOfMemory,
    InvalidState,
};

enum class ParseLimit : uint8
{
    None,
    InputBytes,
    WorkspaceBytes,
    NestingDepth,
    ObjectMembers,
    ArrayElements,
    Values,
    StringBytes,
    OutputBytes,
};

inline constexpr usize UNKNOWN_BYTE_OFFSET = ~usize{0};

// Byte offsets, never character indices. A DOM validation error may have no
// source offset; do not render UNKNOWN_BYTE_OFFSET as a location in the file.
struct ParseError final
{
    ParseStatus Status = ParseStatus::Ok;
    usize Offset = UNKNOWN_BYTE_OFFSET;
    ParseLimit Limit = ParseLimit::None;
    usize Budget = 0;
    usize Observed = 0;
};

struct SourceRange final
{
    usize Offset = 0;
    usize Length = 0;
};

struct SourcePosition final
{
    usize Line = 1;
    usize ByteColumn = 1;
};

// Checked, borrowed input. Failed operations leave both cursor and output
// unchanged. There is no native-layout cast, allocation, or hidden I/O.
class ByteCursor final
{
public:
    explicit ByteCursor(std::span<const uint8> input) noexcept : mInput(input) {}
    [[nodiscard]] usize Position() const noexcept
    {
        return mPosition;
    }
    [[nodiscard]] usize Remaining() const noexcept
    {
        return mInput.size() - mPosition;
    }
    [[nodiscard]] bool Take(usize count, std::span<const uint8>& output) noexcept;
    [[nodiscard]] bool Skip(usize count) noexcept;
    [[nodiscard]] bool ReadUint8(uint8& output) noexcept;
    [[nodiscard]] bool ReadUint32LittleEndian(uint32& output) noexcept;
    [[nodiscard]] bool ReadUint64LittleEndian(uint64& output) noexcept;

private:
    std::span<const uint8> mInput;
    usize mPosition = 0;
};

// Validates Unicode scalar values, including shortest encoding and surrogate
// exclusion. Embedded NUL is valid UTF-8; each format decides whether to allow it.
[[nodiscard]] ParseStatus ValidateUtf8(std::string_view input, ParseError& error) noexcept;
// On-demand, linear location lookup. CRLF is one newline; bare CR is a newline.
// Accepts an offset at EOF. Out-of-range offsets preserve output.
[[nodiscard]] bool LocateByte(std::string_view input, usize offset, SourcePosition& output) noexcept;
} // namespace ludus::foundation::parsing
