#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/time/time.hpp>

namespace ludus::foundation::time
{
/// Lifecycle state of an owner-local elapsed-time accumulator.
enum class StopwatchState : uint8
{
    /// No interval has been started; Read returns InvalidState.
    Stopped,
    /// Read includes time since the most recent Start or Resume.
    Running,
    /// Read returns accumulated time without including the paused interval.
    Paused
};

/// @brief Owner-local value.
///
/// Supply one timestamp to all timers in an update; use synthetic samples in tests/replay. Samples must share a
/// monotonic domain.
/// Failed transitions/reads preserve state/output. Read is observational and
/// validates against the last transition, not previous observational reads.
class Stopwatch final
{
public:
    /// Discard accumulated time and enter Stopped state.
    void Reset() noexcept;
    /// Start a new interval at now, discarding all previous accumulated time.
    void Start(Timestamp now) noexcept;
    /// Accumulate through now and pause; invalid state, regression or overflow preserves state.
    /// @return Ok only from Running with a valid, representable interval.
    [[nodiscard]] TimeStatus Pause(Timestamp now) noexcept;
    /// Resume a Paused interval, excluding paused time; other states return InvalidState.
    /// ClockRegression preserves state when now precedes the pause sample.
    [[nodiscard]] TimeStatus Resume(Timestamp now) noexcept;
    /// Read accumulated elapsed time without changing state; failure preserves out.
    /// Paused ignores now; Running checks regression and overflow; Stopped returns InvalidState.
    [[nodiscard]] TimeStatus Read(Timestamp now, Duration& out) const noexcept;
    /// Return the current lifecycle state without sampling the clock.
    [[nodiscard]] StopwatchState GetState() const noexcept
    {
        return mState;
    }

private:
    Timestamp mAnchor;
    Duration mAccumulated;
    StopwatchState mState = StopwatchState::Stopped;
};

/// @brief A real-time timeout, not a gameplay tick deadline.
///
/// Zero delay is immediately expired. An inactive flag distinguishes cancellation from deadline zero.
/// Arm failure preserves the previous deadline. Expiration does not disarm it.
class Deadline final
{
public:
    /// Set the due timestamp to now plus delay; Overflow preserves the previous deadline.
    [[nodiscard]] TimeStatus Arm(Timestamp now, Duration delay) noexcept;
    /// Disarm the timeout; retain the stored due timestamp.
    void Cancel() noexcept
    {
        mArmed = false;
    }
    /// Whether Arm succeeded more recently than Cancel.
    [[nodiscard]] bool IsArmed() const noexcept
    {
        return mArmed;
    }
    /// True when armed and now is at or past the due timestamp; does not disarm.
    [[nodiscard]] bool IsExpired(Timestamp now) const noexcept
    {
        return mArmed && now.Nanoseconds >= mDue.Nanoseconds;
    }
    /// Return the stored due timestamp; use IsArmed before interpreting it as an active deadline.
    [[nodiscard]] Timestamp GetDue() const noexcept
    {
        return mDue;
    }

private:
    Timestamp mDue;
    bool mArmed = false;
};

/// Result of one frame clock sample, including time discarded by the application clamp.
struct FrameSample
{
    /// Accepted frame interval, at most the configured maximum delta.
    Duration Elapsed{};
    /// Valid elapsed time excluded by the maximum-delta clamp.
    Duration Discarded{};
    /// Ok for accepted samples; ClockRegression reports a zero interval without moving the baseline.
    TimeStatus Status = TimeStatus::Ok;
    /// True for the first sample after construction or Reset; both intervals are zero.
    bool IsBaseline = false;
};

/// @brief Sample once per frame and pass the result by value.
///
/// Reset at startup, pause, resume, suspension and session replacement. First sample yields zero elapsed.
/// Maximum delta is application policy; zero deliberately freezes accepted time.
/// A backwards sample yields zero and retains the previous high-water mark, so
/// recovery cannot count the same interval twice. Equal samples are accepted.
class FrameClock final
{
public:
    /// Configure the accepted delta cap in nanoseconds; zero freezes accepted elapsed time.
    explicit FrameClock(Duration maximumDelta) noexcept : mMaximumDelta(maximumDelta) {}
    /// Forget the previous sample; the next Sample establishes a zero-duration baseline.
    void Reset() noexcept;
    /// Sample elapsed time, retaining the high-water mark on regression and reporting clamp loss.
    [[nodiscard]] FrameSample Sample(Timestamp now) noexcept;

private:
    Duration mMaximumDelta;
    Timestamp mPrevious;
    bool mHasBaseline = false;
};
} // namespace ludus::foundation::time
