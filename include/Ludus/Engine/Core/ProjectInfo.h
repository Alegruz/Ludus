#pragma once

#include <Ludus/Engine/Core/Container/String.hpp>
#include <cstdint>

#define LUDUS_MAKE_API_VERSION(variant, major, minor, patch) \
    ((((uint32_t)(variant)) << 29U) | (((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))

namespace ludus::core
{
    struct ProjectInfo final
    {
        String Name = "Ludus Application";
        uint32_t Version = LUDUS_MAKE_API_VERSION(0, 0, 1, 0);
    };
}  // namespace ludus::core
