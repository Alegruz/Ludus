#pragma once

#include <Ludus/Engine/Core/Compiler.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <source_location>
#include <string>
#include <utility>

namespace ludus::core
{
    enum class LogLevel : uint8_t
    {
        Trace = 0,
        Debug,
        Info,
        Warning,
        Error,
        Fatal,
        Off,
    };

    struct LogMessage final
    {
        LogLevel Level = LogLevel::Info;
        const char* Category = nullptr;
        const char* File = nullptr;
        const char* Function = nullptr;
        uint32_t Line = 0;
        const char* Text = nullptr;
        size_t TextLength = 0;
    };

    using LogSink = void (*)(const LogMessage& message, void* userData) noexcept;

    [[nodiscard]] constexpr LogLevel GetDefaultLogLevel() noexcept
    {
#if defined(LUDUS_DEBUG)
        return LogLevel::Trace;
#else
        return LogLevel::Info;
#endif
    }

    [[nodiscard]] const char* ToString(LogLevel level) noexcept;

    void SetLogLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel GetLogLevel() noexcept;
    [[nodiscard]] bool WouldLog(LogLevel level) noexcept;

    [[nodiscard]] bool AddLogSink(LogSink sink, void* userData = nullptr) noexcept;
    [[nodiscard]] bool RemoveLogSink(LogSink sink, void* userData = nullptr) noexcept;

    void SetStandardLogSinkEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsStandardLogSinkEnabled() noexcept;

    void SetDebuggerLogSinkEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsDebuggerLogSinkEnabled() noexcept;

    void ResetLogger() noexcept;

    void LogWrite(
        LogLevel level,
        const char* category,
        const char* file,
        const char* function,
        uint32_t line,
        const char* message,
        size_t messageLength) noexcept;

    void LogWrite(
        LogLevel level,
        const char* category,
        const std::source_location& location,
        const char* message,
        size_t messageLength) noexcept;

    LUDUS_INLINE void LogWrite(
        LogLevel level,
        const char* category,
        const char* file,
        const char* function,
        uint32_t line,
        const char* message) noexcept
    {
        const char* text = message ? message : "";
        LogWrite(level, category, file, function, line, text, std::char_traits<char>::length(text));
    }

    LUDUS_INLINE void LogWrite(
        LogLevel level,
        const char* category,
        const std::source_location& location,
        const char* message) noexcept
    {
        const char* text = message ? message : "";
        LogWrite(level, category, location, text, std::char_traits<char>::length(text));
    }

    template<typename... Args>
    void LogFormat(
        LogLevel level,
        const char* category,
        const std::source_location& location,
        std::format_string<Args...> format,
        Args&&... args)
    {
        constexpr size_t STACK_BUFFER_SIZE = 1024;

        char stackBuffer[STACK_BUFFER_SIZE];
        const auto result = std::format_to_n(
            stackBuffer,
            STACK_BUFFER_SIZE - 1,
            format,
            std::forward<Args>(args)...);

        const size_t requiredLength = result.size;
        if (requiredLength < STACK_BUFFER_SIZE)
        {
            stackBuffer[requiredLength] = '\0';
            LogWrite(level, category, location, stackBuffer, requiredLength);
            return;
        }

        std::string heapBuffer(requiredLength + 1, '\0');
        std::format_to_n(
            heapBuffer.data(),
            requiredLength,
            format,
            std::forward<Args>(args)...);

        heapBuffer[requiredLength] = '\0';
        LogWrite(level, category, location, heapBuffer.data(), requiredLength);
    }
} // namespace ludus::core

// Logging macros
//
// Usage:
//   LUDUS_LOG_INFO("RHI", "Initialized backend {}", backendName);
//   LUDUS_LOG_WARNING("Renderer", "Shader {} took {} ms to compile", shaderName, compileMs);
//   LUDUS_LOG_ERROR("Asset", "Failed to load '{}'", assetPath);
//   LUDUS_LOG_DEBUG("RHI", "Mask bin={:#010b} oct={:#06o} hex={:#010x}", mask, mask, mask);
//
// Notes:
//   - `category` should be a short subsystem tag such as "RHI", "Renderer", "Core", or "Asset".
//   - The format string is type-checked at compile time via `std::format`.
//   - Arguments are only formatted when the requested level passes the current runtime filter.
//   - Source file, function, and line are captured automatically.
//   - Integer base formatting uses standard `std::format` specifiers:
//       `{:b}` / `{:B}`   binary
//       `{:o}`            octal
//       `{:x}` / `{:X}`   hexadecimal
//       `{:#b}` / `{:#B}` adds `0b` / `0B`
//       `{:#x}` / `{:#X}` adds `0x` / `0X`
//       `{:#o}`           adds leading `0`
//   - Recommended engine-facing convention:
//       use `#` when the base matters to the reader, especially for masks, flags, handles, and GPU bits
//       pad to the full field width when the value is effectively fixed-width, e.g. `{:#010x}` for 32-bit words
//       for binary, include the prefix and zero-pad when bit layout matters, e.g. `{:#018b}` for 16 bits
//
// Generic form:
//   LUDUS_LOG(LogLevel::Debug, "RHI", "Adapter '{}' has {} queue families", adapterName, queueFamilyCount);
#define LUDUS_LOG(level, category, format, ...)                                                          \
    do                                                                                                   \
    {                                                                                                    \
        if (::ludus::core::WouldLog(level))                                                              \
        {                                                                                                \
            ::ludus::core::LogFormat(                                                                    \
                level,                                                                                   \
                category,                                                                                \
                ::std::source_location::current(),                                                       \
                format __VA_OPT__(,) __VA_ARGS__);                                                       \
        }                                                                                                \
    } while (false)

#define LUDUS_LOG_TRACE(category, format, ...) LUDUS_LOG(::ludus::core::LogLevel::Trace, category, format __VA_OPT__(,) __VA_ARGS__)
#define LUDUS_LOG_DEBUG(category, format, ...) LUDUS_LOG(::ludus::core::LogLevel::Debug, category, format __VA_OPT__(,) __VA_ARGS__)
#define LUDUS_LOG_INFO(category, format, ...) LUDUS_LOG(::ludus::core::LogLevel::Info, category, format __VA_OPT__(,) __VA_ARGS__)
#define LUDUS_LOG_WARNING(category, format, ...) LUDUS_LOG(::ludus::core::LogLevel::Warning, category, format __VA_OPT__(,) __VA_ARGS__)
#define LUDUS_LOG_ERROR(category, format, ...) LUDUS_LOG(::ludus::core::LogLevel::Error, category, format __VA_OPT__(,) __VA_ARGS__)
#define LUDUS_LOG_FATAL(category, format, ...) LUDUS_LOG(::ludus::core::LogLevel::Fatal, category, format __VA_OPT__(,) __VA_ARGS__)
