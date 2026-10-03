#pragma once

// Private accessors for the generated host ABI identity.

#include <ludus/foundation/base/types.h>

#include <string_view>

namespace ludus::runtime::game_host
{
using ludus::foundation::uint32;

// The full host compatibility identity string (SDK variant | compiler |
// target | abi). Mirrored into generation manifests and compared against a
// module's embedded Query identity before Create.
[[nodiscard]] std::string_view CurrentHostIdentity() noexcept;
[[nodiscard]] uint32 CurrentAbiMajor() noexcept;
[[nodiscard]] uint32 CurrentAbiMinor() noexcept;
} // namespace ludus::runtime::game_host
