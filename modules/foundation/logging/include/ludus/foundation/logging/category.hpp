#pragma once

#include <ludus/foundation/base/types.h>

#include <string_view>

namespace ludus::foundation::logging
{

// Compile-time FNV-1a hash used to derive a stable 32-bit category id from the
// category name. Runtime filtering compares these integer ids rather than
// performing a string-map lookup on every logging call
// (.kiro/specs/logging-redesign/design.md section 3).
[[nodiscard]] constexpr uint32 HashLogCategory(std::string_view name) noexcept
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

// A logging category. Instances are expected to be declared `inline constexpr`
// at namespace scope so their id is computed at compile time and no dynamic
// registration is needed on the logging hot path.
//
// The descriptor stays a literal type (no atomics inside it): per-category
// runtime overrides live in a separate process-lifetime registry keyed by id
// (see log_system.hpp / the internal registry), so a category can be declared
// in a header and filtered safely before any registration runs, and so the
// hot-path predicate is a small set of scalar loads with no lock or map lookup
// (requirements R13).
struct LogCategory
{
    uint32 Id;
    std::string_view Name;

    consteval LogCategory(std::string_view category_name) noexcept
        : Id(HashLogCategory(category_name)), Name(category_name)
    {
    }

    // Explicit two-argument form kept for parity with the specification's
    // examples and for tests that construct categories at runtime.
    constexpr LogCategory(uint32 category_id, std::string_view category_name) noexcept
        : Id(category_id), Name(category_name)
    {
    }

    [[nodiscard]] friend constexpr bool operator==(LogCategory lhs, LogCategory rhs) noexcept
    {
        return lhs.Id == rhs.Id && lhs.Name == rhs.Name;
    }
};

// Declaration / definition macros (design.md section 2). Declaring a category in
// a header pulls no heavy machinery; the definition lives in exactly one TU with
// constant initialization and process-stable lifetime. These are thin
// conveniences over the constexpr constructor; existing `inline constexpr
// LogCategory X{"Name"}` definitions remain valid.
#define LUDUS_DECLARE_LOG_CATEGORY(symbol) extern const ::ludus::foundation::logging::LogCategory symbol

#define LUDUS_DEFINE_LOG_CATEGORY(symbol, name)                                                                        \
    inline constexpr ::ludus::foundation::logging::LogCategory symbol                                                  \
    {                                                                                                                  \
        std::string_view                                                                                               \
        {                                                                                                              \
            name                                                                                                       \
        }                                                                                                              \
    }

// Foundation-owned categories. Engine modules declare their own categories in
// their own headers; these are the baseline set that the foundation and early
// bring-up code can rely on.
inline constexpr LogCategory LOG_CORE{"Core"};
inline constexpr LogCategory LOG_TEMP{"Temp"};

} // namespace ludus::foundation::logging
