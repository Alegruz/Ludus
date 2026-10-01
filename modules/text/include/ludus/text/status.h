#pragma once

// Shared public status vocabulary for the Ludus text/font rendering system
// (CPU Text and GraphicsText), per .kiro/specs/text-font-rendering/design.md
// section 6. Every public fallible call returns one of these and is noexcept.
//
// This header is intentionally lightweight: it names only an enum over a
// fixed-width base type and pulls in no container, string, or heavy STL header.

#include <ludus/foundation/base/types.h>

namespace ludus::text
{
// `Pending` denotes staged/asynchronous progress, never success that can be
// drawn immediately. Backend/library detail is mapped into these bounded codes.
enum class Status : ludus::foundation::uint8
{
    Ok,
    Pending,
    NotReady,
    InvalidArgument,
    InvalidHandle,
    InvalidUtf8,
    UnsupportedText,
    UnsupportedFont,
    UnsupportedGlyphFormat,
    UnsupportedPresentation,
    NeedsPreparation,
    Busy,
    ResourceLimit,
    OutOfMemory,
    BackendFailure,
    DeviceLost,
};

[[nodiscard]] constexpr bool IsOk(Status status) noexcept
{
    return status == Status::Ok;
}

// Stable, bounded, human-readable name for diagnostics/logging. Never throws
// and never allocates; returns a static string.
[[nodiscard]] const char* ToString(Status status) noexcept;
} // namespace ludus::text
