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
    LogLevel GlobalLevel = LogLevel::Info;
    LogMode Mode = LogMode::Synchronous;

    bool EnableConsole = true;
    bool EnableFile = true;
    bool EnableDebugger = true;

    // Resolved by the platform layer and injected here so that FoundationLogging
    // never depends on the platform module (spec sections 2.1, 21). When empty,
    // the file sink is disabled and a diagnostic is emitted on the emergency
    // path rather than guessing a Directory.
    std::filesystem::path Directory;

    std::uint32_t FlushIntervalMilliseconds = 1000;
    std::uint64_t MaxFileSizeBytes = 32ull * 1024ull * 1024ull;
    std::uint32_t RetainedSessions = 10;
};

// Counters describing logging throughput and back-pressure. In synchronous Mode
// nothing is ever Dropped, but the field exists so callers and tests can rely on
// a stable shape across execution Modes (spec section 17).
struct LogStatistics
{
    std::uint64_t Submitted = 0;
    std::uint64_t Written = 0;
    std::uint64_t Dropped = 0;
};

} // namespace ludus::foundation::logging
