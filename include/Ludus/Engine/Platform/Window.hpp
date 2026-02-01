#pragma once

#include <Ludus/Engine/Platform/Window.h>

#include <Ludus/Engine/Core/Math/Rect.hpp>

namespace ludus::platform
{
    template<PlatformType PLATFORM_TYPE>
    LRESULT CALLBACK Window<PLATFORM_TYPE>::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) requires (PLATFORM_TYPE == PlatformType::WINDOWS)
    {
        Window<PLATFORM_TYPE>* pWindow = nullptr;
        
        if (message == WM_NCCREATE)
        {
            CREATESTRUCT* createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
            pWindow = static_cast<Window<PLATFORM_TYPE>*>(createStruct->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWindow));
            pWindow->mWindowHandle = hwnd;  // Store window handle immediately
        }
        else
        {
            pWindow = reinterpret_cast<Window<PLATFORM_TYPE>*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }
        
        if(pWindow == nullptr)
        {
            return DefWindowProc(hwnd, message, wParam, lParam);
        }

        Window<PLATFORM_TYPE>& window = *pWindow;

        switch (message)
        {
            case WM_DESTROY:
            {
                PostQuitMessage(0);
                return 0;
            }
            default:
            {
                const bool isMessageProcessed = window.mWindowProcedureOrNull != nullptr
                    ? window.mWindowProcedureOrNull(ProcedureParams<PlatformType::WINDOWS>{
                        .OutResult = 0,
                        .WindowHandle = hwnd,
                        .Message = message,
                        .WParam = wParam,
                        .LParam = lParam,
                        .Window = window,
                    })
                    : false;
                    
                if (isMessageProcessed)
                {
                    return 0; // Message was processed by custom procedure
                }

                return DefWindowProc(hwnd, message, wParam, lParam);
            }
        }
    }

    template<PlatformType PLATFORM_TYPE>
    LUDUS_INLINE constexpr Window<PLATFORM_TYPE>::Window(const CreateInfo& createInfo) noexcept
        : mTitle(createInfo.Title)
        , mRect(createInfo.RectOrNull != nullptr ? *createInfo.RectOrNull : core::RectU{})
        , mWindowProcedureOrNull(createInfo.WindowProcedureOrNull)
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
                mRect.X, mRect.Y, mRect.Width, mRect.Height,
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
    LUDUS_INLINE constexpr uint32_t Window<PLATFORM_TYPE>::GetWidth() const noexcept
    {
        return mRect.Width;
    }

    template<PlatformType PLATFORM_TYPE>
    LUDUS_INLINE constexpr uint32_t Window<PLATFORM_TYPE>::GetHeight() const noexcept
    {
        return mRect.Height;
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
    LUDUS_INLINE constexpr WindowManager<PLATFORM_TYPE>::WindowManager() noexcept
        : mDefaultWindowRect{ 100, 100, 800, 600 }
        , mWindows()
    {
    }

    template<PlatformType PLATFORM_TYPE>
    LUDUS_INLINE WindowManager<PLATFORM_TYPE>::~WindowManager() noexcept = default;

    template<PlatformType PLATFORM_TYPE>
    template<core::StringCharType CharT>
    LUDUS_INLINE void WindowManager<PLATFORM_TYPE>::ParseCommandLineWithContext(
        const core::CommandLineManager<CharT>& commandLine) noexcept
    {
        const size_t argCount = commandLine.GetArgumentCount();
        
        for (size_t i = 0; i < argCount; ++i)
        {
            core::BasicString<CharT> arg;
            if (!commandLine.TryGetArgument(i, arg))
            {
                continue;
            }

            core::String argStr;
            if constexpr (std::is_same_v<CharT, wchar_t>)
            {
                argStr = core::ConvertWStringToString(arg);
            }
            else
            {
                argStr = arg;
            }

            // Handle paired arguments (flag + value)
            if (argStr == "--width" || argStr == "-w")
            {
                core::BasicString<CharT> valueArg;
                if (commandLine.TryGetArgument(i + 1, valueArg))
                {
                    core::String valueStr;
                    if constexpr (std::is_same_v<CharT, wchar_t>)
                    {
                        valueStr = core::ConvertWStringToString(valueArg);
                    }
                    else
                    {
                        valueStr = valueArg;
                    }
                    
                    // Parse width value
                    if (!valueStr.IsEmpty())
                    {
                        int32_t width = 0;
                        if (core::StringToInt32(valueStr.GetCStr(), width) && width > 0)
                        {
                            this->mDefaultWindowRect.Width = static_cast<uint32_t>(width);
                            ++i; // Skip the value argument
                        }
                    }
                }
            }
            else if (argStr == "--height" || argStr == "-h")
            {
                core::BasicString<CharT> valueArg;
                if (commandLine.TryGetArgument(i + 1, valueArg))
                {
                    core::String valueStr;
                    if constexpr (std::is_same_v<CharT, wchar_t>)
                    {
                        valueStr = core::ConvertWStringToString(valueArg);
                    }
                    else
                    {
                        valueStr = valueArg;
                    }
                    
                    // Parse height value
                    if (!valueStr.IsEmpty())
                    {
                        int32_t height = 0;
                        if (core::StringToInt32(valueStr.GetCStr(), height) && height > 0)
                        {
                            this->mDefaultWindowRect.Height = static_cast<uint32_t>(height);
                            ++i; // Skip the value argument
                        }
                    }
                }
            }
        }
    }

    template<PlatformType PLATFORM_TYPE>
    LUDUS_INLINE Window<PLATFORM_TYPE>& WindowManager<PLATFORM_TYPE>::CreateWindow(const typename Window<PLATFORM_TYPE>::CreateInfo& createInfo) noexcept
    {
        typename Window<PLATFORM_TYPE>::CreateInfo info = createInfo;
        if(info.RectOrNull == nullptr)
        {
            info.RectOrNull = &mDefaultWindowRect;
        }
        mWindows.PushBack(core::MakeUnique<Window<PLATFORM_TYPE>>(info));
        return *mWindows.GetBack();
    }
}   // namespace ludus::platform