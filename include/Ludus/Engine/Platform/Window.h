#pragma once

#include <Ludus/Engine/Platform/Platform.h>

#include <Ludus/Engine/Core/Math/Rect.h>
#include <Ludus/Engine/Core/Container/String.h>

#undef CreateWindow

namespace ludus::platform
{
    template<PlatformType PLATFORM_TYPE>
    class Window
    {
    public:
        template<PlatformType PT>
        friend class WindowManager;

    public:
        struct CreateInfo final
        {
            core::String Title;
            core::RectU* RectOrNull = nullptr;
            [[no_unique_address]] std::conditional_t<PLATFORM_TYPE == PlatformType::WINDOWS, HINSTANCE, std::monostate> Instance;
        };

    private:
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) requires (PLATFORM_TYPE == PlatformType::WINDOWS);
    
    public:
        ~Window() noexcept;

        // Accessors
        [[nodiscard]] constexpr const core::String& GetTitle() const noexcept;
        [[nodiscard]] constexpr const core::RectU& GetRect() const noexcept;

        void Show(const int32_t commandShowFlag) const noexcept;

    private:
        constexpr explicit Window(const CreateInfo& createInfo) noexcept;

    private:
        core::String mTitle;
        core::RectU mRect;
        // Initialize platform handles so failure paths remain safe to call.
        [[no_unique_address]] std::conditional_t<PLATFORM_TYPE == PlatformType::WINDOWS, HINSTANCE, std::monostate> mInstance{};
        [[no_unique_address]] std::conditional_t<PLATFORM_TYPE == PlatformType::WINDOWS, HWND, std::monostate> mWindowHandle{};
    };

    template<PlatformType PLATFORM_TYPE>
    class WindowManager final
    {
    public:
        constexpr WindowManager() noexcept;
        ~WindowManager() noexcept;

        template<core::StringCharType CharT>
        void HandleArgument(const core::BasicString<CharT>& argument) noexcept;

        // Window Management
        constexpr Window<PLATFORM_TYPE>& CreateWindow(const typename Window<PLATFORM_TYPE>::CreateInfo& createInfo) noexcept;

    private:
        core::RectU mDefaultWindowRect;
        core::DynamicArray<Window<PLATFORM_TYPE>> mWindows;
    };
}   // namespace ludus::platform