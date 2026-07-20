#pragma once

#include <string_view>

namespace ludus::foundation::internal {

[[nodiscard]] constexpr std::string_view normalized_revision(std::string_view revision) noexcept
{
    return revision.empty() ? std::string_view{"unknown"} : revision;
}

} // namespace ludus::foundation::internal
