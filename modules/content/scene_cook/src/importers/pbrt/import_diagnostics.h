#pragma once

// Aggregated, deterministic import diagnostics for the best-effort importers.
// Every entry carries the source file (relative to the import root) and line.

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/strings/string.hpp>

#include <string_view>

namespace ludus::content::scene_cook::importers
{
using foundation::Array;
using foundation::String;
using foundation::core::uint32;
using foundation::core::uint8;
using foundation::core::usize;

/// Severity of one diagnostic; Error entries make the import fail.
enum class DiagnosticSeverity : uint8
{
    Info,
    Warning,
    Error,
};

/// Stable diagnostic codes recorded in the import report.
namespace codes
{
inline constexpr std::string_view UNKNOWN_DIRECTIVE = "UNKNOWN_DIRECTIVE";
inline constexpr std::string_view DIRECTIVE_IGNORED = "DIRECTIVE_IGNORED";
inline constexpr std::string_view DEPRECATED_DIRECTIVE = "DEPRECATED_DIRECTIVE";
inline constexpr std::string_view UNKNOWN_PARAMETER = "UNKNOWN_PARAMETER";
inline constexpr std::string_view SYNTAX_ERROR = "SYNTAX_ERROR";
inline constexpr std::string_view INCLUDE_FAILED = "INCLUDE_FAILED";
inline constexpr std::string_view UNBALANCED_BLOCK = "UNBALANCED_BLOCK";
inline constexpr std::string_view NAME_REDEFINED = "NAME_REDEFINED";
inline constexpr std::string_view NAME_UNDEFINED = "NAME_UNDEFINED";
inline constexpr std::string_view MEDIUM_SKIPPED = "MEDIUM_SKIPPED";
inline constexpr std::string_view SHAPE_SKIPPED = "SHAPE_SKIPPED";
inline constexpr std::string_view SHAPE_FAILED = "SHAPE_FAILED";
inline constexpr std::string_view SUBDIV_SKIPPED = "SUBDIV_SKIPPED";
inline constexpr std::string_view ALPHA_APPROXIMATED = "ALPHA_APPROXIMATED";
inline constexpr std::string_view DISPLACEMENT_IGNORED = "DISPLACEMENT_IGNORED";
inline constexpr std::string_view FACE_INDICES_IGNORED = "FACE_INDICES_IGNORED";
inline constexpr std::string_view MATERIAL_APPROXIMATED = "MATERIAL_APPROXIMATED";
inline constexpr std::string_view MATERIAL_DEFAULTED = "MATERIAL_DEFAULTED";
inline constexpr std::string_view TRANSMISSION_DROPPED = "TRANSMISSION_DROPPED";
inline constexpr std::string_view MIX_APPROXIMATED = "MIX_APPROXIMATED";
inline constexpr std::string_view SPECTRUM_APPROXIMATED = "SPECTRUM_APPROXIMATED";
inline constexpr std::string_view TEXTURE_APPROXIMATED = "TEXTURE_APPROXIMATED";
inline constexpr std::string_view NORMALMAP_DROPPED = "NORMALMAP_DROPPED";
inline constexpr std::string_view TEXTURE_UNREADABLE = "TEXTURE_UNREADABLE";
inline constexpr std::string_view LIGHT_SKIPPED = "LIGHT_SKIPPED";
inline constexpr std::string_view ENVIRONMENT_FALLBACK = "ENVIRONMENT_FALLBACK";
inline constexpr std::string_view CAMERA_APPROXIMATED = "CAMERA_APPROXIMATED";
inline constexpr std::string_view CAMERA_DEFAULTED = "CAMERA_DEFAULTED";
inline constexpr std::string_view INSTANCES_TRUNCATED = "INSTANCES_TRUNCATED";
inline constexpr std::string_view INSTANCES_DROPPED = "INSTANCES_DROPPED";
inline constexpr std::string_view MATERIALS_MERGED = "MATERIALS_MERGED";
inline constexpr std::string_view TEXTURES_DROPPED = "TEXTURES_DROPPED";
inline constexpr std::string_view TEXTURE_DOWNSCALED = "TEXTURE_DOWNSCALED";
inline constexpr std::string_view GEOMETRY_DROPPED = "GEOMETRY_DROPPED";
inline constexpr std::string_view EMISSION_CLAMPED = "EMISSION_CLAMPED";
inline constexpr std::string_view LIMIT_EXCEEDED = "LIMIT_EXCEEDED";
} // namespace codes

/// One aggregated diagnostic: the first occurrence's location plus a count.
struct Diagnostic final
{
    /// Stable code from importers::codes.
    String Code;
    DiagnosticSeverity Severity = DiagnosticSeverity::Info;
    /// Root-relative source file of the first occurrence; empty when not file-bound.
    String File;
    /// One-based line of the first occurrence; zero when not file-bound.
    uint32 Line = 0;
    /// Human-readable detail of the first occurrence.
    String Message;
    /// Number of occurrences folded into this entry (same code and message).
    uint32 Count = 1;
};

/// Insertion-ordered diagnostic log; entries with equal code and message are folded.
class DiagnosticLog final
{
public:
    /// Record one occurrence. Returns false only when memory is exhausted.
    bool Add(DiagnosticSeverity severity,
             std::string_view code,
             std::string_view file,
             uint32 line,
             std::string_view message) noexcept;
    /// Entries in first-occurrence order.
    [[nodiscard]] const Array<Diagnostic>& GetEntries() const noexcept
    {
        return mEntries;
    }
    /// True when an Error entry was recorded.
    [[nodiscard]] bool HasErrors() const noexcept
    {
        return mHasErrors;
    }
    /// True after an allocation failure; later entries may be missing.
    [[nodiscard]] bool HasOverflowed() const noexcept
    {
        return mHasOverflowed;
    }
    /// Total occurrences of code across all entries.
    [[nodiscard]] uint32 CountOccurrences(std::string_view code) const noexcept;

private:
    Array<Diagnostic> mEntries;
    bool mHasErrors = false;
    bool mHasOverflowed = false;
};
} // namespace ludus::content::scene_cook::importers
