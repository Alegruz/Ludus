#include "internal/identity.h"

#include <ludus/runtime/game_host/game_host_identity.hpp>

namespace ludus::runtime::game_host
{
std::string_view CurrentHostIdentity() noexcept
{
    return identity::kIdentityString;
}

uint32 CurrentAbiMajor() noexcept
{
    return identity::kAbiMajor;
}

uint32 CurrentAbiMinor() noexcept
{
    return identity::kAbiMinor;
}
} // namespace ludus::runtime::game_host
