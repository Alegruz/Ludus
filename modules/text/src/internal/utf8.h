#pragma once

// Strict UTF-8 decoder for the CPU text path (design section 3, requirement
// T04). Rejects overlong encodings, invalid continuation bytes, surrogates
// (U+D800..U+DFFF), and code points above U+10FFFF, reporting the byte offset
// of the first invalid byte. No implicit normalization is performed.

#include <ludus/foundation/base/types.h>

#include <string_view>

namespace ludus::text::internal
{
using ludus::foundation::uint32;
using ludus::foundation::usize;

struct Utf8Result final
{
    bool Valid = false;
    usize FirstInvalidByte = 0; // Offset of the first invalid byte when !Valid.
    usize CodePointCount = 0;   // Number of scalar values when Valid.
};

// Validate the whole string strictly. On the first error, Valid is false and
// FirstInvalidByte is the offset at which decoding failed.
[[nodiscard]] Utf8Result ValidateUtf8(std::string_view text) noexcept;

// Decode one scalar value starting at `text[offset]`. Returns the number of
// bytes consumed (1..4) and writes the code point to *outCodePoint, or 0 on an
// invalid sequence. Assumes offset < text.size().
[[nodiscard]] usize DecodeUtf8(std::string_view text, usize offset, uint32* outCodePoint) noexcept;
} // namespace ludus::text::internal
