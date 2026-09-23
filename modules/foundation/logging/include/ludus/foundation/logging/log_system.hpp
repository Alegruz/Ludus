#pragma once

// -----------------------------------------------------------------------------
// Logging lifecycle, configuration, flush and health control surface.
//
// This header carries NO public <filesystem>: the log directory is a borrowed
// UTF-8 path view copied during Initialize() (requirements R21, R47). It is a
// separate, rarely-included header so ordinary call sites (log.hpp /
// log_format.hpp) never pay for configuration types.
//
// See .kiro/specs/logging-redesign/design.md sections 1, 6, 8.
// -----------------------------------------------------------------------------

#include <ludus/foundation/base/types.h>
#include <ludus/foundation/logging/category.hpp>
#include <ludus/foundation/logging/level.hpp>

#include <string_view>

namespace ludus::foundation::logging
{

// Execution strategy for the logging backend. Synchronous is the corrected,
// serialized backend used by tools/tests; Asynchronous selects the bounded MPSC
// backend. The EFFECTIVE mode is reported by Initialize(); there is no live
// SetMode (a live transition would require draining and transferring sink
// ownership). See requirements R41.
enum class LogMode : uint8
{
    Synchronous,
    Asynchronous,
};

// Runtime configuration passed to LogSystem::Initialize.
struct LogConfig
{
    LogLevel GlobalLevel = LogLevel::Info;
    LogMode Mode = LogMode::Synchronous;

    bool EnableConsole = true;
    bool EnableFile = true;
    bool EnableDebugger = true;

    // Writable directory for the session file, resolved by the application /
    // platform bootstrap and injected here as a borrowed UTF-8 path. Copied
    // during Initialize(); the caller need not keep it alive afterward. When
    // empty, the file sink is disabled and a diagnostic is emitted on the
    // emergency path rather than guessing a directory (requirements R47).
    std::string_view Directory;

    // Timed flush cadence for the file sink. Honored by the backend
    // (requirements R46). 0 disables timed flushing (records still flush on
    // shutdown, on urgent events, and on explicit Flush()).
    uint32 FlushIntervalMilliseconds = 1000;

    // Per-session storage bounds. A record-boundary rotation triggers when a
    // segment would exceed MaxFileSizeBytes; MaxTotalBytes / MaxSessionAgeDays
    // bound retained storage across sessions (requirements R44). 0 means
    // "no limit for this dimension" and is documented, not implicit.
    uint64 MaxFileSizeBytes = 32ull * 1024ull * 1024ull;
    uint64 MaxTotalBytes = 0;     // 0 = unbounded total (bounded by RetainedSessions/age)
    uint32 MaxSessionAgeDays = 0; // 0 = no age cap
    uint32 RetainedSessions = 10; // 0 = keep all
};

// Result of Initialize(): status plus the mode actually in effect. A caller can
// observe whether a requested Asynchronous mode was honored or fell back to
// Synchronous / was rejected as Unsupported (requirements R41, R28).
enum class LogStatus : uint8
{
    Ok,
    NotInitialized,
    AlreadyInitialized,
    Unsupported,
    Degraded,
    TimedOut,
    Incomplete,
    SinkFailed,
    Filtered,
};

struct LogInitResult
{
    LogStatus Status = LogStatus::NotInitialized;
    LogMode EffectiveMode = LogMode::Synchronous;
};

// Flush strength. Visible pushes selected local sink buffers to the OS; Durable
// additionally requests a platform durable-file operation (fsync/FlushFileBuffers)
// for selected files. Neither guarantees remote reception or hardware-failure
// survival (requirements R28, R46).
enum class FlushKind : uint8
{
    Visible,
    Durable,
};

struct FlushResult
{
    LogStatus Status = LogStatus::NotInitialized;
    uint64 AcknowledgedSequence = 0; // highest sequence known processed at return
};

// Counters describing logging throughput and back-pressure. Meanings are
// distinct (requirements R48): Submitted counts admitted attempts; Written
// counts records actually handed to at least one sink and reported OK; Dropped
// counts records lost to overflow/failure. A failed delivery never inflates
// Written.
struct LogStatistics
{
    uint64 Submitted = 0;
    uint64 Written = 0;
    uint64 Dropped = 0;
};

// Per-sink and backend health snapshot (requirements R49).
struct LogHealth
{
    bool ConsoleHealthy = true;
    bool FileHealthy = true;
    bool DebuggerHealthy = true;
    bool WorkerHealthy = true;
    uint64 QueueHighWaterMark = 0;
    uint64 DroppedTotal = 0;
};

// Lifecycle and control surface. Normal engine code uses the LUDUS_LOG_* macros
// almost exclusively; this class is for startup, diagnostics, and tools.
class LogSystem
{
public:
    static LogInitResult Initialize(const LogConfig& config);
    static LogStatus Shutdown();

    // Acknowledged flush: waits (up to timeoutMilliseconds) for all records
    // accepted before the call to be processed and the selected local sinks to
    // complete the requested flush kind, then reports status and the
    // acknowledged sequence (requirements R28, R29). A timeout is honest: it
    // cannot cancel an in-progress blocking OS write.
    static FlushResult Flush(FlushKind kind = FlushKind::Visible, uint32 timeoutMilliseconds = 1000);

    static void SetGlobalLevel(LogLevel level);
    static void SetCategoryLevel(LogCategory category, LogLevel level);
    static void ClearCategoryLevels();

    [[nodiscard]] static LogStatistics Statistics();
    [[nodiscard]] static LogHealth Health();

    // True between a successful Initialize() and Shutdown(). Normal code never
    // needs to check this before logging; it exists for tests and diagnostics.
    [[nodiscard]] static bool IsInitialized() noexcept;
};

} // namespace ludus::foundation::logging
