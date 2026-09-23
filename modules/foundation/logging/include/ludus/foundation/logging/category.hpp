#pragma once

#include <ludus/foundation/base/types.h>

#include <string_view>

namespace ludus::foundation::logging
{

// Compile-time FNV-1a hash used to derive a stable 32-bit category id from the
// category name. Runtime filtering compares these integer ids rather than
// performing a string-map lookup on every logging call (spec section 5).
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

// Foundation-owned categories. Engine modules declare their own categories in
// their own headers; these are the baseline set from spec section 5 that the
// foundation and early bring-up code can rely on.
inline constexpr LogCategory LOG_CORE{"Core"};
inline constexpr LogCategory LOG_TEMP{"Temp"};

} // namespace ludus::foundation::logging
