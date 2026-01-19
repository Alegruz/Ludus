#pragma once

#include <Ludus/Engine/Platform/Platform.h>

#if defined(LUDUS_WINDOWS)
    // Windows
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #define UNICODE
    #include <windows.h>

    constexpr wchar_t EDITOR_WINDOW_CLASS_NAME[] = L"LudusEditorWindow";
    constexpr wchar_t EDITOR_WINDOW_TITLE[]      = L"Ludus Editor";
#endif  // defined(LUDUS_WINDOWS)