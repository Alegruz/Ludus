#include <Ludus/Engine/Platform/Windows/Common.h>

#if defined(LUDUS_WINDOWS)
namespace ludus::platform
{
    void PrintWin32Error() noexcept
    {
        const DWORD errorCode = ::GetLastError();
        LPWSTR messageBuffer = nullptr;
        
        const DWORD size = ::FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&messageBuffer),
            0,
            nullptr
        );
        
        if (size > 0 && messageBuffer != nullptr)
        {
            ::OutputDebugStringW(messageBuffer);
            ::LocalFree(messageBuffer);
        }
    }
}   // namespace ludus::platform
#endif // defined(LUDUS_WINDOWS)