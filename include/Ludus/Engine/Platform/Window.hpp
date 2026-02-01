#pragma once

#include <Ludus/Engine/Platform/Window.h>

namespace ludus::platform
{
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
        mWindows.PushBack(core::MakeUnique<Window<PLATFORM_TYPE>>());
        mWindows.GetBack()->Initialize(info);
        return *mWindows.GetBack();
    }
}   // namespace ludus::platform