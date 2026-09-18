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
// fiasco), keeps the state trivially reachable before initialize() for the
// emergency path, and makes the ownership obvious for sanitizer runs.
struct LoggerState
{
    // Guards config-level mutable state: sinks vector, per-category map, mode.
    // A shared_mutex lets the hot path (should_log / dispatch) read levels
    // concurrently while control operations take the write lock.
    std::shared_mutex mutex;

    std::atomic<bool> initialized{false};
    std::atomic<LogLevel> global_level{LogLevel::Info};
    std::atomic<LogMode> mode{LogMode::Synchronous};

    // Per-category overrides keyed by category id (spec section 8). Read under
    // the shared lock; small and rarely mutated.
    std::unordered_map<std::uint32_t, LogLevel> category_levels;

    std::vector<std::unique_ptr<internal::ILogSink>> sinks;

    std::atomic<std::uint64_t> submitted{0};
    std::atomic<std::uint64_t> written{0};
    std::atomic<std::uint64_t> dropped{0};
};

LoggerState& state() noexcept
{
    static LoggerState instance;
    return instance;
}

// Resolve the effective threshold for a category: its override if present,
// otherwise the global level. Caller must hold at least the shared lock.
LogLevel effective_level_locked(const LoggerState& s, LogCategory category) noexcept
{
    const auto found = s.category_levels.find(category.id);
    if (found != s.category_levels.end()) {
        return found->second;
    }
    return s.global_level.load(std::memory_order_relaxed);
}

} // namespace

bool LogSystem::should_log(LogLevel level, LogCategory category) noexcept
{
    LoggerState& s = state();

    // Fatal is always eligible: it must reach the emergency path even before
    // initialize() and even if a category is turned down (spec sections 25, 26).
    if (level == LogLevel::Fatal) {
        return true;
    }

    // Before initialization, only Warning and above are eligible; they go out
    // via the emergency path (spec section 25).
    if (!s.initialized.load(std::memory_order_acquire)) {
        return level >= LogLevel::Warning;
    }

    std::shared_lock lock(s.mutex);
    return level >= effective_level_locked(s, category);
}

bool LogSystem::is_initialized() noexcept
{
    return state().initialized.load(std::memory_order_acquire);
}

void LogSystem::initialize(const LogConfig& config)
{
    LoggerState& s = state();
    std::unique_lock lock(s.mutex);

    if (s.initialized.load(std::memory_order_acquire)) {
        // Idempotent-ish: re-initialization replaces sinks and levels rather
        // than stacking them. Emit a note so double-init is visible in tests.
        internal::emergency_note("LogSystem::initialize called while already initialized; reconfiguring");
        s.sinks.clear();
    }

    s.global_level.store(config.global_level, std::memory_order_relaxed);
    s.mode.store(config.mode, std::memory_order_relaxed);
    s.category_levels.clear();

    if (config.enable_console) {
        s.sinks.push_back(std::make_unique<internal::ConsoleSink>());
    }
    if (config.enable_debugger) {
        s.sinks.push_back(std::make_unique<internal::DebuggerSink>());
    }
    if (config.enable_file) {
        if (config.directory.empty()) {
            internal::emergency_note("file logging requested but LogConfig::directory is empty; file sink disabled");
        }
        else {
            auto file_sink = internal::FileSink::create(config);
            if (file_sink) {
                s.sinks.push_back(std::move(file_sink));
            }
            else {
                internal::emergency_note("failed to open log file; file sink disabled");
            }
        }
    }

    s.initialized.store(true, std::memory_order_release);
}

void LogSystem::shutdown()
{
    LoggerState& s = state();
    std::unique_lock lock(s.mutex);

    if (!s.initialized.load(std::memory_order_acquire)) {
        return;
    }

    for (auto& sink : s.sinks) {
        sink->flush();
    }
    s.sinks.clear();
    s.category_levels.clear();

    // After shutdown, Warning+ still reaches the emergency path (spec section
    // 24); mark uninitialized so dispatch routes there.
    s.initialized.store(false, std::memory_order_release);
}

void LogSystem::flush()
{
    LoggerState& s = state();
    std::shared_lock lock(s.mutex);
    for (auto& sink : s.sinks) {
        sink->flush();
    }
}

void LogSystem::set_mode(LogMode mode)
{
    // Phase 1 only implements Synchronous. Accept the setter so call sites and
    // future async builds share one API; note when async is requested so the
    // behavior gap is visible rather than silent (spec sections 11, 40).
    state().mode.store(mode, std::memory_order_relaxed);
    if (mode == LogMode::Asynchronous) {
        internal::emergency_note("LogMode::Asynchronous requested; Phase 1 backend is synchronous only");
    }
}

void LogSystem::set_global_level(LogLevel level)
{
    state().global_level.store(level, std::memory_order_relaxed);
}

void LogSystem::set_category_level(LogCategory category, LogLevel level)
{
    LoggerState& s = state();
    std::unique_lock lock(s.mutex);
    s.category_levels[category.id] = level;
}

void LogSystem::clear_category_levels()
{
    LoggerState& s = state();
    std::unique_lock lock(s.mutex);
    s.category_levels.clear();
}

LogStatistics LogSystem::statistics()
{
    LoggerState& s = state();
    LogStatistics stats;
    stats.submitted = s.submitted.load(std::memory_order_relaxed);
    stats.written = s.written.load(std::memory_order_relaxed);
    stats.dropped = s.dropped.load(std::memory_order_relaxed);
    return stats;
}

namespace detail {

void dispatch_formatted(LogLevel level,
                        LogCategory category,
                        const std::source_location& location,
                        std::string_view formatted_message)
{
    LoggerState& s = state();
    s.submitted.fetch_add(1, std::memory_order_relaxed);

    // Pre-init / post-shutdown: route through the hardened emergency path. Only
    // Warning+ reaches here because should_log() gates lower levels, but we
    // re-check defensively (spec sections 25, 26).
    if (!s.initialized.load(std::memory_order_acquire)) {
        if (level >= LogLevel::Warning) {
            internal::emergency_log(level, category, formatted_message, location);
        }
        return;
    }

    internal::LogRecordView record;
    record.level = level;
    record.category = category;
    record.message = formatted_message;
    record.file = location.file_name();
    record.function = location.function_name();
    record.line = location.line();
    record.thread_name = internal::current_thread_name();
    record.thread_id = internal::current_thread_id();
    record.timestamp_ns = internal::now_nanoseconds();

    {
        std::shared_lock lock(s.mutex);
        for (auto& sink : s.sinks) {
            sink->write(record);
        }

        // Synchronous durability contract (spec sections 12, 23): for Error and
        // Fatal, flush before returning so the record is externally visible even
        // if the process dies on the next statement. Fatal additionally goes to
        // the emergency path as a belt-and-suspenders guarantee.
        if (level >= LogLevel::Error) {
            for (auto& sink : s.sinks) {
                sink->flush();
            }
        }
    }

    s.written.fetch_add(1, std::memory_order_relaxed);

    if (level == LogLevel::Fatal) {
        internal::emergency_log(level, category, formatted_message, location);
    }
}

} // namespace detail

} // namespace ludus::foundation::logging
