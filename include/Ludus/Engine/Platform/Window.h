#pragma once

#include <Ludus/Engine/Platform/Platform.h>

#include <Ludus/Engine/Core/SmartPtr.hpp>
#include <Ludus/Engine/Core/Container/String.h>
#include <Ludus/Engine/Core/Math/Rect.hpp>

#undef CreateWindow

namespace ludus::core
{
    template<core::StringCharType CharT>
    class CommandLineManager;
}   // namespace ludus::core

namespace ludus::platform
{
    struct WindowMemberVariablesBase
    {
    public:
        core::String Title;
        core::RectU Rect;
        LudusInstance Instance = NULL;
        LudusWindowHandle WindowHandle = NULL;

    public:
        LUDUS_INLINE virtual ~WindowMemberVariablesBase() noexcept
        {
            Instance = NULL;
            WindowHandle = NULL;
        }
    };

#define LUDUS_DECLARATATION_BY_PLATFORM(FUNCTION_SIGNATURE) \
    FUNCTION_SIGNATURE requires (PLATFORM_TYPE == PlatformType::WINDOWS); \
    FUNCTION_SIGNATURE requires (PLATFORM_TYPE == PlatformType::LINUX); \
    FUNCTION_SIGNATURE requires (PLATFORM_TYPE == PlatformType::MAC); \
    FUNCTION_SIGNATURE = delete

    template<PlatformType PLATFORM_TYPE>
    class Window
    {
    public:
        template<PlatformType PT>
        friend class WindowManager;
        
        template<typename T, typename... Args>
        friend core::UniquePtr<T> core::MakeUnique(Args&&... args);

    public:
        struct CreateInfo final
        {
            core::String Title;
            core::RectU* RectOrNull = nullptr;
            LudusInstance Instance;
        };
        
#if defined(LUDUS_WINDOWS)
        using WindowProcedure = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);
#endif  // defined(LUDUS_WINDOWS)
    
    public:
        ~Window() noexcept requires (PLATFORM_TYPE == PlatformType::WINDOWS);
        LUDUS_INLINE ~Window() noexcept = default;

        // Accessors
        [[nodiscard]] LUDUS_INLINE constexpr const core::String& GetTitle() const noexcept { return mMemberVariables->Title; }
        [[nodiscard]] LUDUS_INLINE constexpr const core::RectU& GetRect() const noexcept { return mMemberVariables->Rect; }
        [[nodiscard]] LUDUS_INLINE constexpr uint32_t GetWidth() const noexcept { return mMemberVariables->Rect.Width; }
        [[nodiscard]] LUDUS_INLINE constexpr uint32_t GetHeight() const noexcept { return mMemberVariables->Rect.Height; }

        [[nodiscard]] bool Initialize(const CreateInfo& createInfo) noexcept;
        LUDUS_INLINE void Show(const int32_t commandShowFlag) const noexcept { show(commandShowFlag); }
        [[nodiscard]] LUDUS_INLINE LudusInstance GetPlatformHandle() const noexcept { return mMemberVariables->Instance; }
        [[nodiscard]] LUDUS_INLINE LudusWindowHandle GetWindowHandle() const noexcept { return mMemberVariables->WindowHandle; }

#if defined(LUDUS_WINDOWS)
        void SetWindowProcedure(WindowProcedure windowProc) noexcept requires (PLATFORM_TYPE == PlatformType::WINDOWS);
#endif  // defined(LUDUS_WINDOWS)

    private:
#if defined(LUDUS_WINDOWS)
        static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) requires (PLATFORM_TYPE == PlatformType::WINDOWS);
#endif  // defined(LUDUS_WINDOWS)

    private:
        LUDUS_DECLARATATION_BY_PLATFORM(explicit Window() noexcept);
        LUDUS_DECLARATATION_BY_PLATFORM(bool initialize(const CreateInfo& createInfo) noexcept);
        LUDUS_DECLARATATION_BY_PLATFORM(void show(const int32_t commandShowFlag) const noexcept);

    private:
        core::UniquePtr<WindowMemberVariablesBase>  mMemberVariables;
    };

    template<PlatformType PLATFORM_TYPE>
    class WindowManager final
    {
    public:
        constexpr WindowManager() noexcept;
        ~WindowManager() noexcept;

        // Parse command line arguments with lookahead support for paired arguments
        template<core::StringCharType CharT>
        void ParseCommandLineWithContext(const core::CommandLineManager<CharT>& commandLine) noexcept;

        // Window Management
        core::UniquePtr<Window<PLATFORM_TYPE>>& CreateWindow(const typename Window<PLATFORM_TYPE>::CreateInfo& createInfo) noexcept;

    private:
        core::RectU mDefaultWindowRect;
        core::DynamicArray<core::UniquePtr<Window<PLATFORM_TYPE>>> mWindows;
    };

    template <PlatformType PLATFORM_TYPE>
    LUDUS_INLINE bool Window<PLATFORM_TYPE>::Initialize(const CreateInfo& createInfo) noexcept
    {
        mMemberVariables->Title = createInfo.Title;
        if (createInfo.RectOrNull != nullptr)
        {
            mMemberVariables->Rect = *(createInfo.RectOrNull);
        }
        else
        {
            mMemberVariables->Rect = core::RectU{ 100, 100, 800, 600 }; // Default rect
        }
        return initialize(createInfo);
    }
}   // namespace ludus::platform