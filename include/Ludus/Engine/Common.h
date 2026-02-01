#pragma once

#include <Ludus/Engine/Core/ProjectInfo.h>

namespace ludus
{
    // ENGINE_INFO is a regular inline variable (not constexpr or forceinline)
    // because String uses non-constexpr strlen in its constructor
    inline const core::ProjectInfo ENGINE_INFO
    {
        .Name = "Ludus Engine",
        .Version = LUDUS_MAKE_API_VERSION(0, 0, 1, 0),
    };
} // namespace ludus
