#pragma once

#include <ludus/foundation/logging/level.hpp>

#include <cstdint>
#include <filesystem>

namespace ludus::foundation::logging {

// Execution strategy for the logging backend. Only Synchronous is implemented
// in Phase 1; Asynchronous is reserved so the public API and configuration
// surface do not change when the bounded MPSC backend lands (spec section 11).
enum class LogMode : std::uint8_t
{
    Synchronous,
    Asynchronous,
};

// Runtime configuration passed to LogSystem::Initialize. Field responsibilities
// are fixed by spec section 31; exact defaults may be overridden per build.
struct LogConfig
{
    LogLevel global_level = LogLevel::Info;
    LogMode mode = LogMode::Synchronous;

    bool enable_console = true;
    bool enable_file = true;
    bool enable_debugger = true;

    // Resolved by the platform layer and injected here so that FoundationLogging
    // never depends on the platform module (spec sections 2.1, 21). When empty,
    // the file sink is disabled and a diagnostic is emitted on the emergency
    // path rather than guessing a directory.
    std::filesystem::path directory;

    std::uint32_t flush_interval_milliseconds = 1000;
    std::uint64_t max_file_size_bytes = 32ull * 1024ull * 1024ull;
    std::uint32_t retained_sessions = 10;
};

// Counters describing logging throughput and back-pressure. In synchronous mode
// nothing is ever dropped, but the field exists so callers and tests can rely on
// a stable shape across execution modes (spec section 17).
struct LogStatistics
{
    std::uint64_t submitted = 0;
    std::uint64_t written = 0;
    std::uint64_t dropped = 0;
};

} // namespace ludus::foundation::logging
