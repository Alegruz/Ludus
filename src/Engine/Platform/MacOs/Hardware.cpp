#include <Ludus/Engine/Platform/Hardware.h>
#include <Ludus/Engine/Platform/Platform.h>

#if defined(LUDUS_MAC)
    #include <mach/mach.h>
    #include <sys/sysctl.h>

namespace ludus::platform::hardware
{
    static uint64_t QuerySysctlUint64(const char* name) noexcept
    {
        uint64_t value = 0;
        size_t size = sizeof(value);
        if (::sysctlbyname(name, &value, &size, nullptr, 0) != 0)
        {
            return 0;
        }
        return value;
    }

    RuntimeInfo QueryRuntimeInfo() noexcept
    {
        RuntimeInfo info{};

        info.LogicalCpuCount = static_cast<uint32_t>(QuerySysctlUint64("hw.logicalcpu"));
        info.PageSizeBytes = QuerySysctlUint64("hw.pagesize");
        info.AllocationGranularityBytes = info.PageSizeBytes;
        info.TotalPhysicalMemoryBytes = QuerySysctlUint64("hw.memsize");

        mach_port_t hostPort = ::mach_host_self();
        vm_size_t pageSize = 0;
        if (::host_page_size(hostPort, &pageSize) == KERN_SUCCESS)
        {
            info.PageSizeBytes = static_cast<uint64_t>(pageSize);
            info.AllocationGranularityBytes = info.PageSizeBytes;
        }

        vm_statistics64_data_t vmStats{};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (::host_statistics64(hostPort, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vmStats), &count) == KERN_SUCCESS)
        {
            const uint64_t freePages =
                static_cast<uint64_t>(vmStats.free_count) +
                static_cast<uint64_t>(vmStats.inactive_count) +
                static_cast<uint64_t>(vmStats.speculative_count);
            info.AvailablePhysicalMemoryBytes = freePages * info.PageSizeBytes;
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
#endif  // defined(LUDUS_MAC)
