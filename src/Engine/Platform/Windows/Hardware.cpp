#include <Ludus/Engine/Platform/Hardware.h>
#include <Ludus/Engine/Platform/Platform.h>
#include <Ludus/Engine/Platform/Windows/Common.h>
#include <Ludus/Engine/Core/Common.h>

#if defined(LUDUS_WINDOWS)
namespace ludus::platform::hardware
{
    bool Enforce64BitOrNotify() noexcept
    {
        const HardwareInfo& info = GetHardwareInfo();
        if (info.Compile.PointerSizeBytes == ludus::core::POINTER_SIZE_64BIT)
        {
            return true;
        }

        constexpr const wchar_t* TITLE = L"Ludus Engine - Unsupported Hardware";
        constexpr const wchar_t* MESSAGE =
            L"This build requires a 64-bit CPU/OS.\n"
            L"Please run a 64-bit version of Windows on a 64-bit CPU.";

        ::MessageBoxW(nullptr, MESSAGE, TITLE, MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return false;
    }

    RuntimeInfo QueryRuntimeInfo() noexcept
    {
        RuntimeInfo info{};

        SYSTEM_INFO systemInfo{};
        ::GetNativeSystemInfo(&systemInfo);
        info.LogicalCpuCount = static_cast<uint32_t>(systemInfo.dwNumberOfProcessors);
        info.PageSizeBytes = static_cast<uint64_t>(systemInfo.dwPageSize);
        info.AllocationGranularityBytes = static_cast<uint64_t>(systemInfo.dwAllocationGranularity);

        MEMORYSTATUSEX memoryStatus{};
        memoryStatus.dwLength = sizeof(memoryStatus);
        if (::GlobalMemoryStatusEx(&memoryStatus) != 0)
        {
            info.TotalPhysicalMemoryBytes = memoryStatus.ullTotalPhys;
            info.AvailablePhysicalMemoryBytes = memoryStatus.ullAvailPhys;
        }

        return info;
    }
}   // namespace ludus::platform::hardware

namespace ludus::platform
{
    bool PreflightPlatformOrNotify() noexcept
    {
        hardware::PreloadHardwareInfo();
        return hardware::Enforce64BitOrNotify();
    }
}   // namespace ludus::platform
#endif  // defined(LUDUS_WINDOWS)
