#pragma once

// -----------------------------------------------------------------------------
// Zone identity & descriptors (final design §14, C3).
//
// Instrumentation identity is split into orthogonal axes (C3):
//   * SITE   — a compile-time 32-bit FNV-1a hash of the zone name. Events carry
//              only this id; the name lives once in the descriptor. This mirrors
//              the proven logging LogCategory scheme (consteval hashing, integer
//              comparison, no per-call string work).
//   * CATEGORY — an optional coarse grouping id, also a compile-time hash. It is
//              metadata + capture policy, distinct from the site (C3).
//   * CALLER-CONTEXT — NOT stored here. It is derived off-line from the per-thread
//              begin/end ordering (C1/C3); never a hot-path concept.
//
// A ZoneDescriptor is a literal type intended to be a `static constexpr` local
// at each instrumentation site, so its id/name/source are computed at compile
// time and no dynamic registration happens on the hot path (§4, §18).
//
// This header is part of the ubiquitous include surface: it pulls in only
// <string_view> and <source_location> (both already on logging's allowed list)
// and instantiates no heavy templates.
// -----------------------------------------------------------------------------

#include <ludus/foundation/base/types.h>

#include <source_location>
#include <string_view>

namespace ludus::foundation::profiling
{

// Compile-time FNV-1a (32-bit) — identical construction to the logging category
// hash, kept independent so the two subsystems do not couple. Used for both site
// ids and category ids (C3).
[[nodiscard]] constexpr uint32 HashZoneName(std::string_view name) noexcept
{
    constexpr uint32 FNV_OFFSET_BASIS = 2166136261u;
    constexpr uint32 FNV_PRIME = 16777619u;

    uint32 hash = FNV_OFFSET_BASIS;
    for (const char character : name)
    {
        hash ^= static_cast<uint32>(static_cast<unsigned char>(character));
        hash *= FNV_PRIME;
    }
    return hash;
}

// A coarse capture/display grouping. Declared `inline constexpr` at namespace
// scope, e.g. `LUDUS_PROFILE_CATEGORY(Render, "Render");`. Id 0 (empty name)
// means "uncategorised".
struct ProfileCategory
{
    uint32 Id;
    std::string_view Name;

    consteval ProfileCategory(std::string_view category_name) noexcept
        : Id(HashZoneName(category_name)), Name(category_name)
    {
    }

    constexpr ProfileCategory(uint32 category_id, std::string_view category_name) noexcept
        : Id(category_id), Name(category_name)
    {
    }
};

// The "no category" sentinel.
inline constexpr ProfileCategory PROFILE_CATEGORY_NONE{0u, std::string_view{}};

// Per-site static metadata. One `static constexpr` instance per instrumentation
// site (not per hit): bounded binary-size cost proportional to distinct sites
// (§14, gate G9). Trivially a literal type so it lives in read-only data.
struct ZoneDescriptor
{
    uint32 SiteId;         // HashZoneName(Name)
    uint32 CategoryId;     // ProfileCategory::Id, or 0
    std::string_view Name; // borrowed string literal; process-lifetime
    std::string_view File; // __FILE__ / source_location; process-lifetime
    uint32 Line;

    // constexpr (not consteval): a `static constexpr` site descriptor is still
    // forced to compile-time evaluation by its storage, while LUDUS_PROFILE_
    // FUNCTION can construct one at runtime from the non-constant __func__.
    constexpr ZoneDescriptor(std::string_view zone_name,
                             ProfileCategory category = PROFILE_CATEGORY_NONE,
                             const std::source_location& location = std::source_location::current()) noexcept
        : SiteId(HashZoneName(zone_name)), CategoryId(category.Id), Name(zone_name), File(location.file_name()),
          Line(location.line())
    {
    }
};

} // namespace ludus::foundation::profiling

// Declare an `inline constexpr` profiling category at namespace scope. The name
// is hashed at compile time; runtime comparison is integer-only.
#define LUDUS_PROFILE_CATEGORY(symbol, name)                                                                           \
    inline constexpr ::ludus::foundation::profiling::ProfileCategory symbol                                            \
    {                                                                                                                  \
        ::std::string_view                                                                                             \
        {                                                                                                              \
            name                                                                                                       \
        }                                                                                                              \
    }
