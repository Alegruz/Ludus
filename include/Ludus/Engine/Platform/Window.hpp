#pragma once

#include <Ludus/Engine/Platform/Window.h>

#include <Ludus/Engine/Core/Math/Rect.hpp>

namespace ludus::platform
{
    template<PlatformType PLATFORM_TYPE>
    LRESULT CALLBACK Window<PLATFORM_TYPE>::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) requires (PLATFORM_TYPE == PlatformType::WINDOWS)
    {
        Window<PLATFORM_TYPE>* window = nullptr;
        
        if (uMsg == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
            window = static_cast<Window<PLATFORM_TYPE>*>(createStruct->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }
        else
        {
            window = reinterpret_cast<Window<PLATFORM_TYPE>*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }
        
        switch (uMsg)
        {
            case WM_DESTROY:
            {
                PostQuitMessage(0);
                return 0;
            }
            default:
            {
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
            }
        }
    }

    template<PlatformType PLATFORM_TYPE>
    LUDUS_INLINE constexpr Window<PLATFORM_TYPE>::Window(const CreateInfo& createInfo) noexcept
        : mTitle(createInfo.Title)
        , mRect(createInfo.RectOrNull != nullptr ? *createInfo.RectOrNull : core::RectU{})
    {
        if constexpr (PLATFORM_TYPE == PlatformType::WINDOWS)
        {
            const core::WString titleWStr = core::ConvertStringToWString(mTitle);
            mInstance = createInfo.Instance;
            
            const WNDCLASSEX windowClassEx
            {
                .cbSize        	= sizeof(WNDCLASSEX),
                .style		 	= CS_HREDRAW | CS_VREDRAW,
                .lpfnWndProc   	= Window<PLATFORM_TYPE>::WindowProc,
                .cbClsExtra   	= 0,
                .cbWndExtra   	= 0,
                .hInstance     	= mInstance,
                .hIcon        	= LoadIcon(NULL, IDI_APPLICATION),
                .hCursor      	= LoadCursor(NULL, IDC_ARROW),
                .hbrBackground	= reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
                .lpszMenuName  	= NULL,
                .lpszClassName 	= titleWStr.GetCStr(),
                .hIconSm      	= LoadIcon(NULL, IDI_APPLICATION)
            };

            if( RegisterClassEx(&windowClassEx) == 0 )
            {
                const DWORD error = GetLastError();
                // ERROR_CLASS_ALREADY_EXISTS is fine when creating multiple windows with the same class.
                if (error != ERROR_CLASS_ALREADY_EXISTS)
                {
                    PrintWin32Error();
                    return;
                }
            }
            
            mWindowHandle = CreateWindowEx(
                0,
                titleWStr.GetCStr(),
                titleWStr.GetCStr(),
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                NULL,
                NULL,
                mInstance,
                this  // Pass this pointer
            );

            if (mWindowHandle == NULL)
            {
                PrintWin32Error();
                return;
            }
        }
    }

    template<PlatformType PLATFORM_TYPE>
    LUDUS_INLINE Window<PLATFORM_TYPE>::~Window() noexcept = default;

    template<PlatformType PLATFORM_TYPE>
    LUDUS_INLINE constexpr const core::String& Window<PLATFORM_TYPE>::GetTitle() const noexcept
    {
        return mTitle;
    }

    template<PlatformType PLATFORM_TYPE>
    LUDUS_INLINE constexpr const core::RectU& Window<PLATFORM_TYPE>::GetRect() const noexcept
    {
        return mRect;
    }

    template<PlatformType PLATFORM_TYPE>
    LUDUS_INLINE void Window<PLATFORM_TYPE>::Show(const int32_t commandShowFlag) const noexcept
    {
        if constexpr (PLATFORM_TYPE == PlatformType::WINDOWS)
        {
            if (mWindowHandle != NULL)
            {
                ShowWindow(mWindowHandle, commandShowFlag);
                UpdateWindow(mWindowHandle);
            }
        }
    }

    template<PlatformType PLATFORM_TYPE>
    LUDUS_INLINE constexpr WindowManager<PLATFORM_TYPE>::WindowManager() noexcept = default;

    template<PlatformType PLATFORM_TYPE>
    LUDUS_INLINE WindowManager<PLATFORM_TYPE>::~WindowManager() noexcept = default;

    template<PlatformType PLATFORM_TYPE>
    template<core::StringCharType CharT>
    LUDUS_INLINE void WindowManager<PLATFORM_TYPE>::HandleArgument(const core::BasicString<CharT>& argument) noexcept
    {
        core::String argStr;
        if constexpr (std::is_same_v<CharT, wchar_t>)
        {
            argStr = core::ConvertWStringToString(argument);
        }
        else
        {
            argStr = argument;
        }
        
        if(argStr == "--width" || argStr == "-w")
        {
            // Handle width argument
            this->mDefaultWindowRect.Width = 1024; // Example width
        }
        else if(argStr == "--height" || argStr == "-h")
        {
            // Handle height argument
            this->mDefaultWindowRect.Height = 768; // Example height
        }
    }

    template<PlatformType PLATFORM_TYPE>
    LUDUS_INLINE constexpr Window<PLATFORM_TYPE>& WindowManager<PLATFORM_TYPE>::CreateWindow(const typename Window<PLATFORM_TYPE>::CreateInfo& createInfo) noexcept
    {
        typename Window<PLATFORM_TYPE>::CreateInfo info = createInfo;
        if(info.RectOrNull == nullptr)
        {
            info.RectOrNull = &mDefaultWindowRect;
        }
        mWindows.PushBack( Window<PLATFORM_TYPE>(info) );
        return mWindows.GetBack();
    }
}   // namespace ludus::platform