#pragma once

// C++ Standard Library MUST come first
#include <cstdint>
#include <cstring>

// Then platform and core
#include <Ludus/Engine/Platform/Platform.h>
#include <Ludus/Engine/Core/Common.h>

#if defined(LUDUS_WINDOWS)
    constexpr wchar_t EDITOR_WINDOW_TITLE[]      = L"Ludus Editor";
#endif  // defined(LUDUS_WINDOWS)