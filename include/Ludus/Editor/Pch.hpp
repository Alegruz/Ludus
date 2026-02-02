#pragma once

// C++ Standard Library MUST come first
#include <cstdint>
#include <cstring>

// Then platform and core
#include <Ludus/Engine/Platform/Platform.h>
#include <Ludus/Engine/Core/Common.h>

#if defined(LUDUS_WINDOWS)
    constexpr wchar_t EDITOR_WINDOW_TITLE[]      = L"Ludus Editor";
    constexpr char    EDITOR_APP_TITLE[]         = "Ludus Editor";
    constexpr char8_t EDITOR_APP_TITLE_U8[]      = u8"Ludus Editor";
#endif  // defined(LUDUS_WINDOWS)