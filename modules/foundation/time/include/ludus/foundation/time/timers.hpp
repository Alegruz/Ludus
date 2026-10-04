#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/time/time.hpp>

namespace ludus::foundation::time
{
enum class StopwatchState : uint8
{
    Stopped,
    Running,
    Paused
};

// Owner-local value. Supply one timestamp to all timers in an update; use
// synthetic samples in tests/replay. Samples must share a monotonic domain.
// Failed transitions/reads preserve state/output. Read is observational and
// validates against the last transition, not previous observational reads.
class Stopwatch final
{
public:
    void Reset() noexcept;
    void Start(Timestamp now) noexcept; // Starts a new interval, discarding history.
    [[nodiscard]] TimeStatus Pause(Timestamp now) noexcept;
    [[nodiscard]] TimeStatus Resume(Timestamp now) noexcept;
    [[nodiscard]] TimeStatus Read(Timestamp now, Duration& out) const noexcept;
    [[nodiscard]] StopwatchState GetState() const noexcept
    {
        return mState;
    }

private:
    Timestamp mAnchor;
    Duration mAccumulated;
    StopwatchState mState = StopwatchState::Stopped;
};

// A real-time timeout, not a gameplay tick deadline. Zero delay is immediately
// expired. An inactive flag distinguishes cancellation from deadline zero.
// Arm failure preserves the previous deadline. Expiration does not disarm it.
class Deadline final
{
public:
    [[nodiscard]] TimeStatus Arm(Timestamp now, Duration delay) noexcept;
    void Cancel() noexcept
    {
        mArmed = false;
    }
    [[nodiscard]] bool IsArmed() const noexcept
    {
        return mArmed;
    }
    [[nodiscard]] bool IsExpired(Timestamp now) const noexcept
    {
        return mArmed && now.Nanoseconds >= mDue.Nanoseconds;
    }
    [[nodiscard]] Timestamp GetDue() const noexcept
    {
        return mDue;
    }

private:
    Timestamp mDue;
    bool mArmed = false;
};

struct FrameSample
{
    Duration Elapsed{};
    Duration Discarded{};
    TimeStatus Status = TimeStatus::Ok;
    bool IsBaseline = false;
};

// Sample once per frame and pass the result by value. Reset at startup, pause,
// resume, suspension and session replacement. First sample yields zero elapsed.
// Maximum delta is application policy; zero deliberately freezes accepted time.
// A backwards sample yields zero and retains the previous high-water mark, so
// recovery cannot count the same interval twice. Equal samples are accepted.
class FrameClock final
{
public:
    explicit FrameClock(Duration maximumDelta) noexcept : mMaximumDelta(maximumDelta) {}
    void Reset() noexcept;
    [[nodiscard]] FrameSample Sample(Timestamp now) noexcept;

private:
    Duration mMaximumDelta;
    Timestamp mPrevious;
    bool mHasBaseline = false;
};
} // namespace ludus::foundation::time
