#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/strings/status.hpp>

#include <string_view>

namespace ludus::foundation
{
// Borrowed validation proof, in bytes. Mutating the source invalidates the proof.
// No normalization, grapheme, display-width or Unicode case-folding claims.
class Utf8View final
{
public:
    [[nodiscard]] std::string_view GetView() const noexcept
    {
        return mView;
    }

private:
    std::string_view mView;
    friend StringStatus ValidateUtf8(std::string_view, Utf8View&, usize&) noexcept;
};
// Failure preserves output and reports the first invalid sequence's byte offset.
// U+0000 is valid Unicode. Success sets errorOffset to zero.
[[nodiscard]] StringStatus ValidateUtf8(std::string_view bytes, Utf8View& output, usize& errorOffset) noexcept;
[[nodiscard]] StringStatus ValidateCString(std::string_view bytes) noexcept;
} // namespace ludus::foundation
