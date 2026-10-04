#pragma once

#include <ludus/foundation/base/compiler.h>
#include <ludus/foundation/base/types.h>

namespace ludus::foundation
{

// Compiled target facts, independent of the host machine and runtime devices.
// This descriptor is opt-in; core.h remains the small foundational vocabulary.
enum class TargetOs : uint8
{
    Web = LUDUS_OS_WEB,
    Windows = LUDUS_OS_WINDOWS,
    MacOS = LUDUS_OS_MACOS,
    Linux = LUDUS_OS_LINUX,
    Android = LUDUS_OS_ANDROID,
    IOS = LUDUS_OS_IOS,
};

enum class TargetArch : uint8
{
    Wasm32 = LUDUS_CPU_WASM32,
    Wasm64 = LUDUS_CPU_WASM64,
    X86_64 = LUDUS_CPU_X86_64,
    Arm64 = LUDUS_CPU_ARM64,
};

enum class TargetCompiler : uint8
{
    Clang = LUDUS_CXX_CLANG,
    Gcc = LUDUS_CXX_GCC,
    Msvc = LUDUS_CXX_MSVC,
};

enum class TargetEndian : uint8
{
    Little = LUDUS_ENDIAN_LITTLE,
    Big = LUDUS_ENDIAN_BIG,
};

struct TargetInfo
{
    TargetOs Os;
    TargetArch Arch;
    TargetCompiler Compiler;
    TargetEndian Endian;
    uint32 PointerBits;
};

// Internal linkage: frontend facts describe this translation unit, even when
// compatible objects in an external consumer were compiled by another frontend.
constexpr TargetInfo kTarget
{
    .Os = static_cast<TargetOs>(LUDUS_TARGET_OS),
    .Arch = static_cast<TargetArch>(LUDUS_TARGET_ARCH),
    .Compiler = static_cast<TargetCompiler>(LUDUS_TARGET_COMPILER),
    .Endian = static_cast<TargetEndian>(LUDUS_TARGET_ENDIAN),
    .PointerBits = LUDUS_POINTER_BITS,
};

static_assert(kTarget.PointerBits == sizeof(void*) * 8, "Ludus pointer width must match the compiled ABI");

[[nodiscard]] constexpr const char* TargetOsName(TargetOs value) noexcept
{
    switch (value)
    {
        case TargetOs::Web:
            return "Emscripten";
        case TargetOs::Windows:
            return "Windows";
        case TargetOs::MacOS:
            return "macOS";
        case TargetOs::Linux:
            return "Linux";
        case TargetOs::Android:
            return "Android";
        case TargetOs::IOS:
            return "iOS";
    }
    return "Unknown";
}

[[nodiscard]] constexpr const char* TargetArchName(TargetArch value) noexcept
{
    switch (value)
    {
        case TargetArch::Wasm32:
            return "wasm32";
        case TargetArch::Wasm64:
            return "wasm64";
        case TargetArch::X86_64:
            return "x86_64";
        case TargetArch::Arm64:
            return "arm64";
    }
    return "Unknown";
}

} // namespace ludus::foundation
