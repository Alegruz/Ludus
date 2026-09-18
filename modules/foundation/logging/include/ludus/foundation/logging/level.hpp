#pragma once

#include <cstdint>
#include <string_view>

namespace ludus::foundation::logging {

// Severity levels, ordered from most to least verbose. The underlying integer
// ordering is load-bearing: runtime and compile-time filtering both rely on
// `level >= threshold` comparisons, so the numeric order must never change
// without updating the filtering logic and the compiled-level macros.
enum class LogLevel : std::uint8_t
{
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Fatal = 5,
};

// Fixed-width, right-padded five-character label used by the console and file
// sinks (e.g. "INFO ", "TRACE"). Returns "?????" for out-of-range values so a
// corrupted record can never index out of bounds.
[[nodiscard]] constexpr std::string_view ToPaddedString(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO ";
        case LogLevel::Warning:
            return "WARN ";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
    }
    return "?????";
}

[[nodiscard]] constexpr std::string_view ToString(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace:
            return "Trace";
        case LogLevel::Debug:
            return "Debug";
        case LogLevel::Info:
            return "Info";
        case LogLevel::Warning:
            return "Warning";
        case LogLevel::Error:
            return "Error";
        case LogLevel::Fatal:
            return "Fatal";
    }
    return "Unknown";
}

} // namespace ludus::foundation::logging
