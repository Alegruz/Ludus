#pragma once

#include <ludus/foundation/base/types.h>
#include <ludus/foundation/logging/log_format.hpp> // FormatArg / ArgTag (public, lightweight)

#include <span>
#include <string_view>

namespace ludus::foundation::logging::internal
{

// Reuse the public packing types; the engine adds only the outcome + the
// non-template FormatInto boundary. Nothing here pulls <format>.
using ArgTag = ::ludus::foundation::logging::ArgTag;
using FormatArg = ::ludus::foundation::logging::FormatArg;

struct FormatOutcome
{
    usize BytesWritten = 0;
    bool Truncated = false;   // output did not fit `out`
    bool FormatError = false; // grammar error / arg-count mismatch / unsupported spec
};

// Non-template boundary: parses `fmt`, substitutes `args`, writes UTF-8 into
// `out`. Never allocates, never throws, never terminates. Compiled once
// (design.md section 5; requirements R19/R25/R38).
[[nodiscard]] FormatOutcome
FormatInto(std::span<char> out, std::string_view fmt, std::span<const FormatArg> args) noexcept;

} // namespace ludus::foundation::logging::internal
