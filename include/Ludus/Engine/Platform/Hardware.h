#pragma once

#include <Ludus/Engine/Core/Common.h>

#include <bit>
#include <cstdint>
#include <new>

#ifndef LUDUS_CMAKE_SYSTEM_NAME
    #define LUDUS_CMAKE_SYSTEM_NAME "Unknown"
#endif

#ifndef LUDUS_CMAKE_SYSTEM_ARCH
    #define LUDUS_CMAKE_SYSTEM_ARCH "unknown"
#endif

#ifndef LUDUS_CMAKE_HOST_SYSTEM_NAME
    #define LUDUS_CMAKE_HOST_SYSTEM_NAME "Unknown"
#endif

#ifndef LUDUS_CMAKE_HOST_SYSTEM_ARCH
    #define LUDUS_CMAKE_HOST_SYSTEM_ARCH "unknown"
#endif

#ifndef LUDUS_CMAKE_SIZEOF_VOID_P
    #define LUDUS_CMAKE_SIZEOF_VOID_P 0
#endif

// Preprocessor-time architecture detection
#if defined(_M_X64) || defined(__x86_64__)
    #define LUDUS_HW_PREPROCESS_ARCH "x86_64"
    #define LUDUS_HW_PREPROCESS_ARCH_X86_64 1
#elif defined(_M_IX86) || defined(__i386__)
    #define LUDUS_HW_PREPROCESS_ARCH "x86"
    #define LUDUS_HW_PREPROCESS_ARCH_X86 1
#elif defined(_M_ARM64) || defined(__aarch64__)
    #define LUDUS_HW_PREPROCESS_ARCH "arm64"
    #define LUDUS_HW_PREPROCESS_ARCH_ARM64 1
#elif defined(_M_ARM) || defined(__arm__)
    #define LUDUS_HW_PREPROCESS_ARCH "arm"
    #define LUDUS_HW_PREPROCESS_ARCH_ARM 1
#elif defined(__ppc64__) || defined(__PPC64__)
    #define LUDUS_HW_PREPROCESS_ARCH "ppc64"
    #define LUDUS_HW_PREPROCESS_ARCH_PPC64 1
#elif defined(__ppc__) || defined(__PPC__)
    #define LUDUS_HW_PREPROCESS_ARCH "ppc"
    #define LUDUS_HW_PREPROCESS_ARCH_PPC 1
#else
    #define LUDUS_HW_PREPROCESS_ARCH "unknown"
    #define LUDUS_HW_PREPROCESS_ARCH_UNKNOWN 1
#endif

#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__) || defined(__ppc64__)
    #define LUDUS_HW_PREPROCESS_64BIT 1
#else
    #define LUDUS_HW_PREPROCESS_64BIT 0
#endif

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        #define LUDUS_HW_PREPROCESS_ENDIANNESS "little"
    #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        #define LUDUS_HW_PREPROCESS_ENDIANNESS "big"
    #else
        #define LUDUS_HW_PREPROCESS_ENDIANNESS "unknown"
    #endif
#else
    #define LUDUS_HW_PREPROCESS_ENDIANNESS "unknown"
#endif

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    #define LUDUS_HW_PREPROCESS_SSE2 1
#else
    #define LUDUS_HW_PREPROCESS_SSE2 0
#endif

#if defined(__AVX__)
    #define LUDUS_HW_PREPROCESS_AVX 1
#else
    #define LUDUS_HW_PREPROCESS_AVX 0
#endif

#if defined(__AVX2__) || defined(_M_AVX2)
    #define LUDUS_HW_PREPROCESS_AVX2 1
#else
    #define LUDUS_HW_PREPROCESS_AVX2 0
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    #define LUDUS_HW_PREPROCESS_NEON 1
#else
    #define LUDUS_HW_PREPROCESS_NEON 0
#endif

namespace ludus::platform::hardware
{
    struct CMakeInfo final
    {
        const char* SystemName;
        const char* SystemArch;
        const char* HostSystemName;
        const char* HostSystemArch;
        uint32_t PointerSizeBytes;
    };

    struct PreprocessInfo final
    {
        const char* Architecture;
        const char* Endianness;
        bool Is64Bit;
        bool HasSse2;
        bool HasAvx;
        bool HasAvx2;
        bool HasNeon;
    };

    struct CompileTimeInfo final
    {
        uint32_t PointerSizeBytes;
        uint32_t CacheLineBytes;
        bool IsLittleEndian;
    };

    struct RuntimeInfo final
    {
        uint32_t LogicalCpuCount;
        uint64_t PageSizeBytes;
        uint64_t AllocationGranularityBytes;
        uint64_t TotalPhysicalMemoryBytes;
        uint64_t AvailablePhysicalMemoryBytes;
    };

    struct HardwareInfo final
    {
        CMakeInfo CMake;
        PreprocessInfo Preprocess;
        CompileTimeInfo Compile;
        RuntimeInfo Runtime;
    };

    [[nodiscard]] constexpr CMakeInfo GetCMakeInfo() noexcept
    {
        return CMakeInfo{
            .SystemName = LUDUS_CMAKE_SYSTEM_NAME,
            .SystemArch = LUDUS_CMAKE_SYSTEM_ARCH,
            .HostSystemName = LUDUS_CMAKE_HOST_SYSTEM_NAME,
            .HostSystemArch = LUDUS_CMAKE_HOST_SYSTEM_ARCH,
            .PointerSizeBytes = static_cast<uint32_t>(LUDUS_CMAKE_SIZEOF_VOID_P),
        };
    }

    [[nodiscard]] constexpr PreprocessInfo GetPreprocessInfo() noexcept
    {
        return PreprocessInfo{
            .Architecture = LUDUS_HW_PREPROCESS_ARCH,
            .Endianness = LUDUS_HW_PREPROCESS_ENDIANNESS,
            .Is64Bit = (LUDUS_HW_PREPROCESS_64BIT != 0),
            .HasSse2 = (LUDUS_HW_PREPROCESS_SSE2 != 0),
            .HasAvx = (LUDUS_HW_PREPROCESS_AVX != 0),
            .HasAvx2 = (LUDUS_HW_PREPROCESS_AVX2 != 0),
            .HasNeon = (LUDUS_HW_PREPROCESS_NEON != 0),
        };
    }

    [[nodiscard]] constexpr CompileTimeInfo GetCompileTimeInfo() noexcept
    {
        uint32_t cacheLineBytes = 64;
        #if defined(__cpp_lib_hardware_interference_size)
            cacheLineBytes = static_cast<uint32_t>(std::hardware_destructive_interference_size);
        #endif

        return CompileTimeInfo{
            .PointerSizeBytes = static_cast<uint32_t>(sizeof(void*)),
            .CacheLineBytes = cacheLineBytes,
            .IsLittleEndian = (std::endian::native == std::endian::little),
        };
    }

    [[nodiscard]] RuntimeInfo QueryRuntimeInfo() noexcept;
    [[nodiscard]] const HardwareInfo& GetHardwareInfo() noexcept;
    [[nodiscard]] bool Enforce64BitOrNotify() noexcept;

    [[nodiscard]] inline HardwareInfo QueryHardwareInfo() noexcept
    {
        HardwareInfo info{};

        // Query order: configure-time -> preprocessing-time -> compile-time -> runtime-preload.
        info.CMake = GetCMakeInfo();
        info.Preprocess = GetPreprocessInfo();
        info.Compile = GetCompileTimeInfo();
        info.Runtime = QueryRuntimeInfo();

        return info;
    }

    [[nodiscard]] inline const HardwareInfo& GetHardwareInfo() noexcept
    {
        static const HardwareInfo info = QueryHardwareInfo();
        return info;
    }

    inline void PreloadHardwareInfo() noexcept
    {
        (void)GetHardwareInfo();
    }
}   // namespace ludus::platform::hardware
