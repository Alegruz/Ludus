#include <ludus/foundation/logging/log.hpp>

#include "internal/emergency_logger.hpp"
#include "internal/formatter.hpp"
#include "internal/log_record.hpp"
#include "internal/sink.hpp"
#include "sinks/console_sink.hpp"
#include "sinks/debugger_sink.hpp"
#include "sinks/file_sink.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ludus::foundation::logging {

namespace {

// All mutable logger state lives behind a single struct with a function-local
// static instance. This gives well-defined construction order (no static-init
// fiasco), keeps the state trivially reachable before Initialize() for the
// emergency path, and makes the ownership obvious for sanitizer runs.
struct LoggerState
{
    // Guards config-level mutable state: sinks vector, per-category map, mode.
    // A shared_mutex lets the hot path (ShouldLog / dispatch) read levels
    // concurrently while control operations take the write lock.
    std::shared_mutex Mutex;

    std::atomic<bool> Initialized{false};
    std::atomic<LogLevel> GlobalLevel{LogLevel::Info};
    std::atomic<LogMode> Mode{LogMode::Synchronous};

    // Per-category overrides keyed by category id (spec section 8). Read under
    // the shared lock; small and rarely mutated.
    std::unordered_map<std::uint32_t, LogLevel> CategoryLevels;

    std::vector<std::unique_ptr<internal::ILogSink>> Sinks;

    std::atomic<std::uint64_t> Submitted{0};
    std::atomic<std::uint64_t> Written{0};
    std::atomic<std::uint64_t> Dropped{0};
};

LoggerState& state() noexcept
{
    static LoggerState sInstance;
    return sInstance;
}

// Resolve the effective threshold for a category: its override if present,
// otherwise the global level. Caller must hold at least the shared lock.
LogLevel effectiveLevelLocked(const LoggerState& s, LogCategory category) noexcept
{
    const auto found = s.CategoryLevels.find(category.Id);
    if (found != s.CategoryLevels.end()) {
        return found->second;
    }
    return s.GlobalLevel.load(std::memory_order_relaxed);
}

} // namespace

bool LogSystem::ShouldLog(LogLevel level, LogCategory category) noexcept
{
    LoggerState& s = state();

    // Fatal is always eligible: it must reach the emergency path even before
    // Initialize() and even if a category is turned down (spec sections 25, 26).
    if (level == LogLevel::Fatal) {
        return true;
    }

    // Before initialization, only Warning and above are eligible; they go out
    // via the emergency path (spec section 25).
    if (!s.Initialized.load(std::memory_order_acquire)) {
        return level >= LogLevel::Warning;
    }

    std::shared_lock lock(s.Mutex);
    return level >= effectiveLevelLocked(s, category);
}

bool LogSystem::IsInitialized() noexcept
{
    return state().Initialized.load(std::memory_order_acquire);
}

void LogSystem::Initialize(const LogConfig& config)
{
    LoggerState& s = state();
    std::unique_lock lock(s.Mutex);

    if (s.Initialized.load(std::memory_order_acquire)) {
        // Idempotent-ish: re-initialization replaces sinks and levels rather
        // than stacking them. Emit a note so double-init is visible in tests.
        internal::EmergencyNote("LogSystem::Initialize called while already initialized; reconfiguring");
        s.Sinks.clear();
    }

    s.GlobalLevel.store(config.GlobalLevel, std::memory_order_relaxed);
    s.Mode.store(config.Mode, std::memory_order_relaxed);
    s.CategoryLevels.clear();

    if (config.EnableConsole) {
        s.Sinks.push_back(std::make_unique<internal::ConsoleSink>());
    }
    if (config.EnableDebugger) {
        s.Sinks.push_back(std::make_unique<internal::DebuggerSink>());
    }
    if (config.EnableFile) {
        if (config.Directory.empty()) {
            internal::EmergencyNote("file logging requested but LogConfig::Directory is empty; file sink disabled");
        }
        else {
            auto file_sink = internal::FileSink::Create(config);
            if (file_sink) {
                s.Sinks.push_back(std::move(file_sink));
            }
            else {
                internal::EmergencyNote("failed to open log file; file sink disabled");
            }
        }
    }

    s.Initialized.store(true, std::memory_order_release);
}

void LogSystem::Shutdown()
{
    LoggerState& s = state();
    std::unique_lock lock(s.Mutex);

    if (!s.Initialized.load(std::memory_order_acquire)) {
        return;
    }

    for (auto& sink : s.Sinks) {
        sink->Flush();
    }
    s.Sinks.clear();
    s.CategoryLevels.clear();

    // After Shutdown(), Warning+ still reaches the emergency path (spec section
    // 24); mark uninitialized so dispatch routes there.
    s.Initialized.store(false, std::memory_order_release);
}

void LogSystem::Flush()
{
    LoggerState& s = state();
    std::shared_lock lock(s.Mutex);
    for (auto& sink : s.Sinks) {
        sink->Flush();
    }
}

void LogSystem::SetMode(LogMode mode)
{
    // Phase 1 only implements Synchronous. Accept the setter so call sites and
    // future async builds share one API; note when async is requested so the
    // behavior gap is visible rather than silent (spec sections 11, 40).
    state().Mode.store(mode, std::memory_order_relaxed);
    if (mode == LogMode::Asynchronous) {
        internal::EmergencyNote("LogMode::Asynchronous requested; Phase 1 backend is synchronous only");
    }
}

void LogSystem::SetGlobalLevel(LogLevel level)
{
    state().GlobalLevel.store(level, std::memory_order_relaxed);
}

void LogSystem::SetCategoryLevel(LogCategory category, LogLevel level)
{
    LoggerState& s = state();
    std::unique_lock lock(s.Mutex);
    s.CategoryLevels[category.Id] = level;
}

void LogSystem::ClearCategoryLevels()
{
    LoggerState& s = state();
    std::unique_lock lock(s.Mutex);
    s.CategoryLevels.clear();
}

LogStatistics LogSystem::Statistics()
{
    LoggerState& s = state();
    LogStatistics stats;
    stats.Submitted = s.Submitted.load(std::memory_order_relaxed);
    stats.Written = s.Written.load(std::memory_order_relaxed);
    stats.Dropped = s.Dropped.load(std::memory_order_relaxed);
    return stats;
}

namespace detail {

void DispatchFormatted(LogLevel level,
                       LogCategory category,
                       const std::source_location& location,
                       std::string_view formatted_message)
{
    LoggerState& s = state();
    s.Submitted.fetch_add(1, std::memory_order_relaxed);

    // Pre-init / post-shutdown: route through the hardened emergency path. Only
    // Warning+ reaches here because ShouldLog() gates lower levels, but we
    // re-check defensively (spec sections 25, 26).
    if (!s.Initialized.load(std::memory_order_acquire)) {
        if (level >= LogLevel::Warning) {
            internal::EmergencyLog(level, category, formatted_message, location);
        }
        return;
    }

    internal::LogRecordView record;
    record.Level = level;
    record.Category = category;
    record.Message = formatted_message;
    record.File = location.file_name();
    record.Function = location.function_name();
    record.Line = location.line();
    record.ThreadName = internal::CurrentThreadName();
    record.ThreadId = internal::CurrentThreadId();
    record.TimestampNs = internal::NowNanoseconds();

    {
        std::shared_lock lock(s.Mutex);
        for (auto& sink : s.Sinks) {
            sink->Write(record);
        }

        // Synchronous durability contract (spec sections 12, 23): for Error and
        // Fatal, flush before returning so the record is externally visible even
        // if the process dies on the next statement. Fatal additionally goes to
        // the emergency path as a belt-and-suspenders guarantee.
        if (level >= LogLevel::Error) {
            for (auto& sink : s.Sinks) {
                sink->Flush();
            }
        }
    }

    s.Written.fetch_add(1, std::memory_order_relaxed);

    if (level == LogLevel::Fatal) {
        internal::EmergencyLog(level, category, formatted_message, location);
    }
}

} // namespace detail

} // namespace ludus::foundation::logging
