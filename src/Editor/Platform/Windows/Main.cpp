#include <Ludus/Editor/Pch.hpp>

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, [[maybe_unused]] int commandShowFlag)
{
    WNDCLASSEX windowClassEx
    {
        .lpfnWndProc   = WindowProc,
        .hInstance     = instance,
        .lpszClassName = EDITOR_WINDOW_CLASS_NAME,
    };

    RegisterClassEx(&windowClassEx);

    HWND hwnd = CreateWindowEx(
        0,
        EDITOR_WINDOW_CLASS_NAME,
        EDITOR_WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL,
        NULL,
        instance,
        nullptr
    );
    if (hwnd == NULL)
    {
        return 0;
    }

    ShowWindow(hwnd, commandShowFlag);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}