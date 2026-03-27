#include <Ludus/Engine/Core/Logger.h>

#if defined(LUDUS_WINDOWS)
    #include <Ludus/Engine/Platform/Windows/Common.h>
#endif

#include <array>
#include <atomic>
#include <cstdio>

namespace ludus::core
{
    namespace
    {
        constexpr uint32_t MAX_LOG_SINKS = 8;

        struct LogSinkSlot final
        {
            LogSink Sink = nullptr;
            void* UserData = nullptr;
        };

        struct LoggerState final
        {
            std::atomic<uint8_t> ActiveLevel = static_cast<uint8_t>(GetDefaultLogLevel());
            std::atomic<bool> StandardSinkEnabled = true;
            std::atomic<bool> DebuggerSinkEnabled = true;
            std::atomic_flag SinkLock = ATOMIC_FLAG_INIT;
            std::array<LogSinkSlot, MAX_LOG_SINKS> Sinks{};
        };

        LoggerState& GetLoggerState() noexcept
        {
            static LoggerState state;
            return state;
        }

        void LockSinks(LoggerState& state) noexcept
        {
            while (state.SinkLock.test_and_set(std::memory_order_acquire))
            {
            }
        }

        void UnlockSinks(LoggerState& state) noexcept
        {
            state.SinkLock.clear(std::memory_order_release);
        }

        [[nodiscard]] const char* GetFileName(const char* path) noexcept
        {
            if (path == nullptr)
            {
                return "<unknown>";
            }

            const char* fileName = path;
            for (const char* cursor = path; *cursor != '\0'; ++cursor)
            {
                if (*cursor == '/' || *cursor == '\\')
                {
                    fileName = cursor + 1;
                }
            }

            return fileName;
        }

        void WriteStandardLogSink(const LogMessage& message) noexcept
        {
            char prefix[384];
            const char* category = (message.Category != nullptr && message.Category[0] != '\0') ? message.Category : "General";
            const char* file = GetFileName(message.File);

            const int prefixLength = std::snprintf(
                prefix,
                sizeof(prefix),
                "[%s][%s] %s:%u | ",
                ToString(message.Level),
                category,
                file,
                message.Line);

            FILE* stream = (message.Level >= LogLevel::Error) ? stderr : stdout;
            if (prefixLength > 0)
            {
                const size_t safePrefixLength = static_cast<size_t>(prefixLength);
                const size_t clampedPrefixLength = safePrefixLength < sizeof(prefix) ? safePrefixLength : sizeof(prefix) - 1;
                (void)std::fwrite(prefix, 1, clampedPrefixLength, stream);
            }

            if (message.Text != nullptr && message.TextLength > 0)
            {
                (void)std::fwrite(message.Text, 1, message.TextLength, stream);
            }

            (void)std::fwrite("\n", 1, 1, stream);
            (void)std::fflush(stream);
        }

#if defined(LUDUS_WINDOWS)
        void WriteDebuggerLogSink(const LogMessage& message) noexcept
        {
            char buffer[1536];
            const char* category = (message.Category != nullptr && message.Category[0] != '\0') ? message.Category : "General";
            const char* file = GetFileName(message.File);

            const int written = std::snprintf(
                buffer,
                sizeof(buffer),
                "[%s][%s] %s:%u | %.*s\n",
                ToString(message.Level),
                category,
                file,
                message.Line,
                static_cast<int>(message.TextLength),
                message.Text != nullptr ? message.Text : "");

            if (written > 0)
            {
                ::OutputDebugStringA(buffer);
            }
        }
#endif
    } // namespace

    const char* ToString(const LogLevel level) noexcept
    {
        switch (level)
        {
            case LogLevel::Trace: return "Trace";
            case LogLevel::Debug: return "Debug";
            case LogLevel::Info: return "Info";
            case LogLevel::Warning: return "Warning";
            case LogLevel::Error: return "Error";
            case LogLevel::Fatal: return "Fatal";
            case LogLevel::Off: return "Off";
            default: return "Unknown";
        }
    }

    void SetLogLevel(const LogLevel level) noexcept
    {
        GetLoggerState().ActiveLevel.store(static_cast<uint8_t>(level), std::memory_order_release);
    }

    LogLevel GetLogLevel() noexcept
    {
        return static_cast<LogLevel>(GetLoggerState().ActiveLevel.load(std::memory_order_acquire));
    }

    bool WouldLog(const LogLevel level) noexcept
    {
        return level >= GetLogLevel() && level != LogLevel::Off;
    }

    bool AddLogSink(LogSink sink, void* userData) noexcept
    {
        if (sink == nullptr)
        {
            return false;
        }

        LoggerState& state = GetLoggerState();
        LockSinks(state);

        for (const LogSinkSlot& slot : state.Sinks)
        {
            if (slot.Sink == sink && slot.UserData == userData)
            {
                UnlockSinks(state);
                return true;
            }
        }

        for (LogSinkSlot& slot : state.Sinks)
        {
            if (slot.Sink == nullptr)
            {
                slot.Sink = sink;
                slot.UserData = userData;
                UnlockSinks(state);
                return true;
            }
        }

        UnlockSinks(state);
        return false;
    }

    bool RemoveLogSink(LogSink sink, void* userData) noexcept
    {
        if (sink == nullptr)
        {
            return false;
        }

        LoggerState& state = GetLoggerState();
        LockSinks(state);

        for (LogSinkSlot& slot : state.Sinks)
        {
            if (slot.Sink == sink && slot.UserData == userData)
            {
                slot.Sink = nullptr;
                slot.UserData = nullptr;
                UnlockSinks(state);
                return true;
            }
        }

        UnlockSinks(state);
        return false;
    }

    void SetStandardLogSinkEnabled(const bool enabled) noexcept
    {
        GetLoggerState().StandardSinkEnabled.store(enabled, std::memory_order_release);
    }

    bool IsStandardLogSinkEnabled() noexcept
    {
        return GetLoggerState().StandardSinkEnabled.load(std::memory_order_acquire);
    }

    void SetDebuggerLogSinkEnabled(const bool enabled) noexcept
    {
        GetLoggerState().DebuggerSinkEnabled.store(enabled, std::memory_order_release);
    }

    bool IsDebuggerLogSinkEnabled() noexcept
    {
        return GetLoggerState().DebuggerSinkEnabled.load(std::memory_order_acquire);
    }

    void ResetLogger() noexcept
    {
        LoggerState& state = GetLoggerState();
        SetLogLevel(GetDefaultLogLevel());
        state.StandardSinkEnabled.store(true, std::memory_order_release);
        state.DebuggerSinkEnabled.store(true, std::memory_order_release);

        LockSinks(state);
        for (LogSinkSlot& slot : state.Sinks)
        {
            slot.Sink = nullptr;
            slot.UserData = nullptr;
        }
        UnlockSinks(state);
    }

    void LogWrite(
        const LogLevel level,
        const char* category,
        const char* file,
        const char* function,
        const uint32_t line,
        const char* message,
        const size_t messageLength) noexcept
    {
        if (!WouldLog(level))
        {
            return;
        }

        const LogMessage logMessage{
            .Level = level,
            .Category = category,
            .File = file,
            .Function = function,
            .Line = line,
            .Text = message != nullptr ? message : "",
            .TextLength = messageLength,
        };

        LoggerState& state = GetLoggerState();

        if (state.StandardSinkEnabled.load(std::memory_order_acquire))
        {
            WriteStandardLogSink(logMessage);
        }

#if defined(LUDUS_WINDOWS)
        if (state.DebuggerSinkEnabled.load(std::memory_order_acquire))
        {
            WriteDebuggerLogSink(logMessage);
        }
#endif

        std::array<LogSinkSlot, MAX_LOG_SINKS> sinkSnapshot{};
        LockSinks(state);
        sinkSnapshot = state.Sinks;
        UnlockSinks(state);

        for (const LogSinkSlot& slot : sinkSnapshot)
        {
            if (slot.Sink != nullptr)
            {
                slot.Sink(logMessage, slot.UserData);
            }
        }
    }

    void LogWrite(
        const LogLevel level,
        const char* category,
        const std::source_location& location,
        const char* message,
        const size_t messageLength) noexcept
    {
        LogWrite(
            level,
            category,
            location.file_name(),
            location.function_name(),
            static_cast<uint32_t>(location.line()),
            message,
            messageLength);
    }
} // namespace ludus::core
