#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/config/config.hpp>

namespace ludus::runtime::configuration
{
// First migrated module-owned schema; bump identity for incompatible contracts.
inline constexpr std::string_view HOST_SCHEMA = "ludus.host.v1";
struct HostOptions final
{
    bool Headless = false;
    foundation::uint64 MaxFrames = 0; // Zero means unlimited.
};
[[nodiscard]] std::span<const foundation::config::Descriptor> HostSchema() noexcept;
[[nodiscard]] foundation::config::Status ReadHostOptions(const foundation::config::Context& context,
                                                         HostOptions& output) noexcept;
} // namespace ludus::runtime::configuration
