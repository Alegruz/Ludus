#include <Ludus/Engine/Platform/Window.hpp>

#if defined(LUDUS_WINDOWS)
#include <Ludus/Engine/Core/SmartPtr.hpp>
#include <Ludus/Engine/Core/Container/Array.hpp>

namespace ludus::platform
{
    struct WindowMemberVariablesWindows final : public WindowMemberVariablesBase    // NOLINT(cppcoreguidelines-special-member-functions)
    {
        Window<PlatformType::WINDOWS>::WindowProcedure WindowProcedureOrNull = nullptr;
        HINSTANCE Instance = NULL;
        HWND WindowHandle = NULL;

        ~WindowMemberVariablesWindows() noexcept override
        {
            WindowProcedureOrNull = nullptr;
            Instance = NULL;
            WindowHandle = NULL;
        }
    };

#define mMemberVariablesWindowsOf(ptr) (*static_cast<std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(ptr)>>, const WindowMemberVariablesWindows*, WindowMemberVariablesWindows*>>((ptr).mMemberVariables.Get()))   // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast,-warnings-as-errors)
#define mMemberVariablesWindows mMemberVariablesWindowsOf(*this)

    template<PlatformType PLATFORM_TYPE>
    LRESULT CALLBACK Window<PLATFORM_TYPE>::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) requires (PLATFORM_TYPE == PlatformType::WINDOWS)
    {
        Window<PLATFORM_TYPE>* pWindow = nullptr;
        
        if (message == WM_NCCREATE)
        {
            CREATESTRUCT* createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);   // NOLINT(performance-no-int-to-ptr)
            pWindow = static_cast<Window<PLATFORM_TYPE>*>(createStruct->lpCreateParams);
            if(pWindow == nullptr)
            {
                return DefWindowProc(hwnd, message, wParam, lParam);
            }
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWindow));
            mMemberVariablesWindowsOf(*pWindow).WindowHandle = hwnd;  // Store window handle immediately
        }
        else
        {
            pWindow = reinterpret_cast<Window<PLATFORM_TYPE>*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));  // NOLINT(performance-no-int-to-ptr)
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
                if(mMemberVariablesWindowsOf(window).WindowProcedureOrNull != nullptr)
                {
                    return mMemberVariablesWindowsOf(window).WindowProcedureOrNull(hwnd, message, wParam, lParam);
                }
                
                return DefWindowProc(hwnd, message, wParam, lParam);
            }
        }
    }

    template<PlatformType PLATFORM_TYPE>
    Window<PLATFORM_TYPE>::~Window() noexcept requires (PLATFORM_TYPE == PlatformType::WINDOWS)
    {
        if (mMemberVariablesWindows.WindowHandle != NULL)
        {
            DestroyWindow(mMemberVariablesWindows.WindowHandle);
            mMemberVariablesWindows.WindowHandle = NULL;
        }
    }

    template<PlatformType PLATFORM_TYPE>
    Window<PLATFORM_TYPE>::Window() noexcept requires (PLATFORM_TYPE == PlatformType::WINDOWS)
        : mMemberVariables(core::MakeUnique<WindowMemberVariablesWindows>())
    {
    }

    template<PlatformType PLATFORM_TYPE>
    void Window<PLATFORM_TYPE>::SetWindowProcedure(WindowProcedure windowProc) noexcept requires (PLATFORM_TYPE == PlatformType::WINDOWS)
    {
        mMemberVariablesWindows.WindowProcedureOrNull = windowProc;
    }

    template<PlatformType PLATFORM_TYPE>
    bool Window<PLATFORM_TYPE>::initialize(const CreateInfo& createInfo) noexcept requires (PLATFORM_TYPE == PlatformType::WINDOWS)
    {
        const core::WString titleWStr = core::ConvertStringToWString(mMemberVariablesWindows.Title);
        mMemberVariablesWindows.Instance = createInfo.Instance;
        const WNDCLASSEX windowClassEx
        {
            .cbSize        	= sizeof(WNDCLASSEX),
            .style		 	= CS_HREDRAW | CS_VREDRAW,
            .lpfnWndProc   	= Window<PLATFORM_TYPE>::windowProc,
            .cbClsExtra   	= 0,
            .cbWndExtra   	= 0,
            .hInstance     	= mMemberVariablesWindows.Instance,
            .hIcon        	= LoadIcon(NULL, IDI_APPLICATION),
            .hCursor      	= LoadCursor(NULL, IDC_ARROW),
            .hbrBackground	= reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),   // NOLINT(performance-no-int-to-ptr)
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
                return false;
            }
        }
        
        mMemberVariablesWindows.WindowHandle = CreateWindowEx(
            0,
            titleWStr.GetCStr(),
            titleWStr.GetCStr(),
            WS_OVERLAPPEDWINDOW,
            mMemberVariablesWindows.Rect.X, mMemberVariablesWindows.Rect.Y, mMemberVariablesWindows.Rect.Width, mMemberVariablesWindows.Rect.Height,
            NULL,
            NULL,
            mMemberVariablesWindows.Instance,
            this  // Pass this pointer
        );

        if (mMemberVariablesWindows.WindowHandle == NULL)
        {
            PrintWin32Error();
            return false;
        }

        return true;
    }
    
    template<PlatformType PLATFORM_TYPE>
    void Window<PLATFORM_TYPE>::show(const int32_t commandShowFlag) const noexcept requires (PLATFORM_TYPE == PlatformType::WINDOWS)
    {
        if (mMemberVariablesWindows.WindowHandle != NULL)
        {
            ShowWindow(mMemberVariablesWindows.WindowHandle, commandShowFlag);
            UpdateWindow(mMemberVariablesWindows.WindowHandle);
        }
    }

    template<PlatformType PLATFORM_TYPE>
    uint64_t Window<PLATFORM_TYPE>::getPlatformHandle() const noexcept requires (PLATFORM_TYPE == PlatformType::WINDOWS)
    {
        return reinterpret_cast<uint64_t>(mMemberVariablesWindows.WindowHandle);
    }

    template class Window<PlatformType::WINDOWS>;
    template class WindowManager<PlatformType::WINDOWS>;
}   // namespace ludus::platform
#endif  // defined(LUDUS_WINDOWS)