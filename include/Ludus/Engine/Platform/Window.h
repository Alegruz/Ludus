#pragma once

#include <Ludus/Engine/Platform/Platform.h>

#include <Ludus/Engine/Core/Math/Rect.h>
#include <Ludus/Engine/Core/Container/String.h>
#include <Ludus/Engine/Core/SmartPtr.h>

#undef CreateWindow

namespace ludus::platform
{
    template<PlatformType PLATFORM_TYPE>
    class Window
    {
    public:
        template<PlatformType PT>
        friend class WindowManager;
        
        template<typename T, typename... Args>
        friend core::UniquePtr<T> core::MakeUnique(Args&&... args);

    public:
        template<PlatformType PT>
        struct ProcedureParams final
        {
        };

        template<>
        struct ProcedureParams<PlatformType::WINDOWS> final
        {
            LRESULT OutResult;
            HWND WindowHandle;
            UINT Message;
            WPARAM WParam;
            LPARAM LParam;
            Window<PLATFORM_TYPE>& Window;
        };

        template<PlatformType PT>
        using WindowProcType = bool(*)(const ProcedureParams<PT>& params);

        struct CreateInfo final
        {
            core::String Title;
            core::RectU* RectOrNull = nullptr;
            WindowProcType<PLATFORM_TYPE> WindowProcedureOrNull = nullptr;
            [[no_unique_address]] std::conditional_t<PLATFORM_TYPE == PlatformType::WINDOWS, HINSTANCE, std::monostate> Instance;
        };

    private:
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) requires (PLATFORM_TYPE == PlatformType::WINDOWS);
    
    public:
        ~Window() noexcept;

        // Accessors
        [[nodiscard]] constexpr const core::String& GetTitle() const noexcept;
        [[nodiscard]] constexpr const core::RectU& GetRect() const noexcept;
        [[nodiscard]] constexpr uint32_t GetWidth() const noexcept;
        [[nodiscard]] constexpr uint32_t GetHeight() const noexcept;

        void Show(const int32_t commandShowFlag) const noexcept;

    private:
        constexpr explicit Window(const CreateInfo& createInfo) noexcept;

    private:
        core::String mTitle;
        core::RectU mRect;
        WindowProcType<PLATFORM_TYPE> mWindowProcedureOrNull;
        // Initialize platform handles so failure paths remain safe to call.
        [[no_unique_address]] std::conditional_t<PLATFORM_TYPE == PlatformType::WINDOWS, HINSTANCE, std::monostate> mInstance;
        [[no_unique_address]] std::conditional_t<PLATFORM_TYPE == PlatformType::WINDOWS, HWND, std::monostate> mWindowHandle;
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
        Window<PLATFORM_TYPE>& CreateWindow(const typename Window<PLATFORM_TYPE>::CreateInfo& createInfo) noexcept;

    private:
        core::RectU mDefaultWindowRect;
        core::DynamicArray<core::UniquePtr<Window<PLATFORM_TYPE>>> mWindows;
    };
}   // namespace ludus::platform