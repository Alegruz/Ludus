#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::foundation::time
{
// Distinct domains: a duration is an interval; a timestamp is a reading of the
// process's monotonic CPU clock. Neither is UTC, a GPU counter or a game tick.
struct Duration
{
    uint64 Nanoseconds = 0;
    bool operator==(const Duration&) const noexcept = default;
};

struct Timestamp
{
    uint64 Nanoseconds = 0;
    bool operator==(const Timestamp&) const noexcept = default;
};

enum class TimeStatus : uint8
{
    Ok,
    Overflow,
    ClockRegression,
    InvalidState
};

inline constexpr uint64 NANOSECONDS_PER_SECOND = 1'000'000'000;

// Same source/epoch as profiling and logging. Nanoseconds are the storage unit,
// not a resolution guarantee. No engine allocation, lock or initialization.
// Readings are non-decreasing on one thread; equal readings are valid.
// Timestamps do not establish a happens-before relation between threads.
[[nodiscard]] uint64 NowTicks() noexcept;
[[nodiscard]] inline Timestamp Now() noexcept
{
    return {NowTicks()};
}

// Outputs are unchanged on failure. Addition never wraps. Elapsed rejects
// reversed samples. Convert the resulting small interval, not two large epochs.
[[nodiscard]] TimeStatus TryAdd(Duration left, Duration right, Duration& out) noexcept;
[[nodiscard]] TimeStatus TryAdd(Timestamp start, Duration interval, Timestamp& out) noexcept;
[[nodiscard]] TimeStatus TryElapsed(Timestamp start, Timestamp end, Duration& out) noexcept;
[[nodiscard]] constexpr float64 ToSeconds(Duration interval) noexcept
{
    return static_cast<float64>(interval.Nanoseconds) / static_cast<float64>(NANOSECONDS_PER_SECOND);
}
} // namespace ludus::foundation::time
