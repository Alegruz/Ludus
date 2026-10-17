#include <ludus/foundation/base/checked_integer.hpp>
#include <ludus/foundation/base/config.h>
#include <ludus/foundation/base/types.h>
#include <ludus/foundation/time/time.hpp>
#include <ludus/platform/frame_pacing.h>

#if LUDUS_TARGET_OS == LUDUS_OS_MACOS || LUDUS_TARGET_OS == LUDUS_OS_LINUX
#    define LUDUS_PLATFORM_HAS_PACING_WAIT 1
// POSIX nanosleep(2) and errno only; clock reads stay in FoundationTime (ADR 0003).
#    include <errno.h>
#    include <time.h>
#else
#    define LUDUS_PLATFORM_HAS_PACING_WAIT 0
#endif

// The pacing contract follows docs/architecture/high-resolution-time.md, "Native
// frame pacing". Advancing each target from the previous target, rather than
// sleeping a fixed amount after the frame, is the drift-free periodic phase the
// Gems review adopted from Michael Harvey and Carl S. Marshall, "Scheduling Game
// Events", Game Programming Gems 3, section 1.1, pp. 5-14.

namespace ludus::platform
{
namespace
{
using foundation::uint64;
using foundation::time::Duration;
using foundation::time::Timestamp;
using foundation::time::TimeStatus;

foundation::time::Timestamp ReadSystemNow(void* /*contextOrNull*/) noexcept
{
    return foundation::time::Now();
}

PacingWaitStatus WaitForSystemInterval(void* /*contextOrNull*/, Duration interval) noexcept
{
#if LUDUS_PLATFORM_HAS_PACING_WAIT
    // A relative sleep for the remaining interval keeps the absolute target in the
    // FoundationTime domain: the pacer recomputes the remainder from a fresh Now()
    // after every return, so neither an early nor a late wake accumulates.
    timespec request = {};
    if (foundation::core::TryIntegerCast(interval.Nanoseconds / foundation::time::NANOSECONDS_PER_SECOND,
                                         request.tv_sec) == false)
    {
        return PacingWaitStatus::Failed;
    }
    if (foundation::core::TryIntegerCast(interval.Nanoseconds % foundation::time::NANOSECONDS_PER_SECOND,
                                         request.tv_nsec) == false)
    {
        return PacingWaitStatus::Failed;
    }
    if (::nanosleep(&request, nullptr) == 0)
    {
        return PacingWaitStatus::Completed;
    }
    return errno == EINTR ? PacingWaitStatus::Interrupted : PacingWaitStatus::Failed;
#else
    (void)interval;
    return PacingWaitStatus::Unsupported;
#endif
}

/// Return reading - target, or zero when the reading precedes the target.
[[nodiscard]] Duration GetLateness(Timestamp target, Timestamp reading) noexcept
{
    Duration lateness;
    if (foundation::time::TryElapsed(target, reading, lateness) != TimeStatus::Ok)
    {
        return {};
    }
    return lateness;
}
} // namespace

PacingClock GetSystemPacingClock() noexcept
{
    return
    {
        .ReadNowOrNull = ReadSystemNow,
        .WaitForOrNull = WaitForSystemInterval,
        .ContextOrNull = nullptr,
    };
}

PacingStatus FramePacer::Configure(const PacingPolicy& policy) noexcept
{
    if (policy.Interval.Nanoseconds == 0 || policy.Interval.Nanoseconds > MAXIMUM_PACING_INTERVAL_NANOSECONDS)
    {
        return PacingStatus::InvalidConfiguration;
    }
    mPolicy = policy;
    Reset();
    return PacingStatus::Ok;
}

void FramePacer::Reset() noexcept
{
    mIsScheduled = false;
    mNextTarget = {};
}

bool FramePacer::anchor(Timestamp start) noexcept
{
    mLastReading = start;
    if (foundation::time::TryAdd(start, mPolicy.Interval, mNextTarget) != TimeStatus::Ok)
    {
        Reset();
        return false;
    }
    mIsScheduled = true;
    return true;
}

PaceResult FramePacer::Pace(const PacingClock& clock) noexcept
{
    PaceResult result;
    if (mPolicy.Interval.Nanoseconds == 0)
    {
        result.Outcome = PaceOutcome::Disabled;
        return result;
    }
    if (clock.ReadNowOrNull == nullptr || clock.WaitForOrNull == nullptr)
    {
        result.Outcome = PaceOutcome::InvalidClock;
        return result;
    }
    Timestamp now = clock.ReadNowOrNull(clock.ContextOrNull);
    result.Target = now;
    result.Wake = now;
    if (mIsScheduled == false)
    {
        result.Outcome = anchor(now) == true ? PaceOutcome::Baseline : PaceOutcome::Overflow;
        return result;
    }
    const uint64 interval = mPolicy.Interval.Nanoseconds;
    const Timestamp target = mNextTarget;
    // A reading below the previous one, or a target more than two intervals away,
    // cannot come from a monotonic clock; waiting on it could block indefinitely.
    // One interval bounds a scheduled target; an early start after a failed or
    // limited wait leaves the next target less than two intervals away.
    if (now.Nanoseconds < mLastReading.Nanoseconds ||
        (target.Nanoseconds > now.Nanoseconds && target.Nanoseconds - now.Nanoseconds > 2 * interval))
    {
        result.Outcome = anchor(now) == true ? PaceOutcome::ClockRegression : PaceOutcome::Overflow;
        return result;
    }
    result.Target = target;
    if (now.Nanoseconds < target.Nanoseconds)
    {
        result.Outcome = PaceOutcome::WaitLimit;
        while (result.WaitCount < MAXIMUM_PACING_WAIT_ATTEMPTS)
        {
            const Duration remaining = {target.Nanoseconds - now.Nanoseconds};
            const PacingWaitStatus status = clock.WaitForOrNull(clock.ContextOrNull, remaining);
            ++result.WaitCount;
            const Timestamp previous = now;
            now = clock.ReadNowOrNull(clock.ContextOrNull);
            if (now.Nanoseconds < previous.Nanoseconds)
            {
                result.Wake = now;
                result.Outcome = anchor(now) == true ? PaceOutcome::ClockRegression : PaceOutcome::Overflow;
                return result;
            }
            if (status == PacingWaitStatus::Unsupported || status == PacingWaitStatus::Failed)
            {
                result.Outcome = PaceOutcome::WaitFailed;
                break;
            }
            if (now.Nanoseconds >= target.Nanoseconds)
            {
                result.Outcome = PaceOutcome::Waited;
                break;
            }
            ++result.EarlyWakeCount;
        }
        result.Wake = now;
        result.Lateness = GetLateness(target, now);
        mLastReading = now;
        // Advance from the target, never from the wake: an oversleep is not inherited.
        if (foundation::time::TryAdd(target, mPolicy.Interval, mNextTarget) != TimeStatus::Ok)
        {
            Reset();
            result.Outcome = PaceOutcome::Overflow;
        }
        return result;
    }
    result.Lateness = GetLateness(target, now);
    result.SkippedTargets = result.Lateness.Nanoseconds / interval;
    if (result.Lateness.Nanoseconds > mPolicy.MaximumLateness.Nanoseconds)
    {
        result.Outcome = anchor(now) == true ? PaceOutcome::Rescheduled : PaceOutcome::Overflow;
        return result;
    }
    // Keep the phase: the next target is the first target strictly after now.
    uint64 slots = 0;
    uint64 advance = 0;
    uint64 next = 0;
    mLastReading = now;
    if (foundation::core::TryAdd(result.SkippedTargets, uint64{1}, slots) == false ||
        foundation::core::TryMultiply(slots, interval, advance) == false ||
        foundation::core::TryAdd(target.Nanoseconds, advance, next) == false)
    {
        Reset();
        result.Outcome = PaceOutcome::Overflow;
        return result;
    }
    mNextTarget = {next};
    result.Outcome = PaceOutcome::Late;
    return result;
}
} // namespace ludus::platform
