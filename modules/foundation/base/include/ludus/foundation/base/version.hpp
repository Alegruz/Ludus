#pragma once

#include <string_view>

namespace ludus::foundation {

struct Version
{
    int major;
    int minor;
    int patch;

    [[nodiscard]] friend constexpr bool operator==(Version, Version) noexcept = default;
};

[[nodiscard]] Version version() noexcept;
[[nodiscard]] std::string_view version_string() noexcept;
[[nodiscard]] std::string_view git_revision() noexcept;
[[nodiscard]] std::string_view compiler_identity() noexcept;

} // namespace ludus::foundation
