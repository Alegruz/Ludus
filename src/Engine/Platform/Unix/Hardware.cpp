#include <Ludus/Engine/Core/Common.h>
#include <Ludus/Engine/Platform/Hardware.h>
#include <Ludus/Engine/Platform/Platform.h>
#include <Ludus/Engine/Core/Assert.h>

#if defined(LUDUS_LINUX)
    #include <unistd.h>

namespace ludus::platform::hardware
{
    bool Enforce64BitOrNotify() noexcept
    {
        const HardwareInfo& info = GetHardwareInfo();
        LUDUS_ASSERT_MSG(ludus::core::POINTER_SIZE_64BIT == info.Compile.PointerSizeBytes, "Pointer size must be 8 bytes");
        return info.Compile.PointerSizeBytes == ludus::core::POINTER_SIZE_64BIT;
    }

    RuntimeInfo QueryRuntimeInfo() noexcept
    {
        RuntimeInfo info{};

        const long processors = ::sysconf(_SC_NPROCESSORS_ONLN);
        if (processors > 0)
        {
            info.LogicalCpuCount = static_cast<uint32_t>(processors);
        }

        const long pageSize = ::sysconf(_SC_PAGESIZE);
        if (pageSize > 0)
        {
            info.PageSizeBytes = static_cast<uint64_t>(pageSize);
            info.AllocationGranularityBytes = static_cast<uint64_t>(pageSize);
        }

        #ifdef _SC_PHYS_PAGES
        const long physPages = ::sysconf(_SC_PHYS_PAGES);
        if (physPages > 0 && pageSize > 0)
        {
            info.TotalPhysicalMemoryBytes = static_cast<uint64_t>(physPages) * static_cast<uint64_t>(pageSize);
        }
        #endif

        #ifdef _SC_AVPHYS_PAGES
        const long availPages = ::sysconf(_SC_AVPHYS_PAGES);
        if (availPages > 0 && pageSize > 0)
        {
            info.AvailablePhysicalMemoryBytes = static_cast<uint64_t>(availPages) * static_cast<uint64_t>(pageSize);
        }
        #endif

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
#endif  // defined(LUDUS_LINUX)
