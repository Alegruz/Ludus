#include "internal/frame_pacing.h"
#include "internal/world.h"
#include <catch2/catch_test_macros.hpp>
// Synthetic time only: the injected wait advances a counter instead of sleeping.
using namespace ludus::world_demo;
using ludus::foundation::time::Duration;
using ludus::foundation::time::NANOSECONDS_PER_SECOND;
using ludus::foundation::time::Timestamp;
using ludus::platform::PaceOutcome;
using ludus::platform::PaceResult;
using ludus::platform::PacingClock;
using ludus::platform::PacingWaitStatus;
namespace
{
constexpr uint64 EPOCH = uint64{1} << 62U;
constexpr uint64 MILLISECOND = NANOSECONDS_PER_SECOND / 1000;
constexpr uint64 IDLE_INTERVAL = 16'666'667;
constexpr uint64 CAP_30_INTERVAL = 33'333'333;
/// CPU time spent inside one synthetic frame.
constexpr uint64 FRAME_WORK = 4 * MILLISECOND;
struct SyntheticClock final
{
    uint64 Now = EPOCH;
    uint32 ReadCount = 0;
    uint32 WaitCount = 0;
};
Timestamp ReadSyntheticNow(void* contextOrNull) noexcept
{
    SyntheticClock* clock = static_cast<SyntheticClock*>(contextOrNull);
    ++clock->ReadCount;
    return {clock->Now};
}
PacingWaitStatus WaitSynthetic(void* contextOrNull, Duration interval) noexcept
{
    SyntheticClock* clock = static_cast<SyntheticClock*>(contextOrNull);
    ++clock->WaitCount;
    clock->Now += interval.Nanoseconds;
    return PacingWaitStatus::Completed;
}
PacingClock MakeClock(SyntheticClock& inoutClock) noexcept
{
    return
    {
        .ReadNowOrNull = ReadSyntheticNow,
        .WaitForOrNull = WaitSynthetic,
        .ContextOrNull = &inoutClock,
    };
}
void Load(Session& inoutSession)
{
    REQUIRE(inoutSession.RequestLoad(ExampleLevel()).Error == LevelError::None);
    while (inoutSession.GetLoadStage() != LoadStage::Ready)
    {
        REQUIRE(inoutSession.PollLoad() == Status::Success);
    }
    REQUIRE(inoutSession.Activate());
}
} // namespace
TEST_CASE("Loop pacing validates its cap and keeps idle pacing by default", "[world-demo][pacing]")
{
    LoopPacing pacing;
    REQUIRE(pacing.Configure(0));
    CHECK(pacing.HasLimit() == false);
    CHECK(pacing.GetPacer().GetPolicy().Interval.Nanoseconds == IDLE_INTERVAL);
    CHECK(pacing.GetPacer().GetPolicy().MaximumLateness.Nanoseconds == IDLE_INTERVAL * MAXIMUM_LATE_INTERVALS);
    REQUIRE(pacing.Configure(30));
    CHECK(pacing.HasLimit() == true);
    CHECK(pacing.GetPacer().GetPolicy().Interval.Nanoseconds == CAP_30_INTERVAL);
    CHECK(pacing.Configure(MAXIMUM_FRAMES_PER_SECOND + 1) == false);
    CHECK(pacing.HasLimit() == true);
    CHECK(pacing.GetPacer().GetPolicy().Interval.Nanoseconds == CAP_30_INTERVAL);
    REQUIRE(pacing.Configure(MAXIMUM_FRAMES_PER_SECOND));
    CHECK(pacing.GetPacer().GetPolicy().Interval.Nanoseconds == 1'000'000);
}
TEST_CASE("Without a cap only frames that present nothing wait", "[world-demo][pacing]")
{
    LoopPacing pacing;
    REQUIRE(pacing.Configure(0));
    SyntheticClock synthetic;
    const PacingClock clock = MakeClock(synthetic);
    // Presented frames are paced by the FIFO swapchain: no clock read, no wait.
    for (uint32 frame = 0; frame < 3; ++frame)
    {
        CHECK(pacing.EndFrame(true, clock).Outcome == PaceOutcome::Disabled);
    }
    CHECK(synthetic.ReadCount == 0);
    // Loading or minimized frames anchor an idle schedule, then wait for it.
    CHECK(pacing.EndFrame(false, clock).Outcome == PaceOutcome::Baseline);
    const uint64 anchor = synthetic.Now;
    for (uint64 frame = 1; frame <= 5; ++frame)
    {
        synthetic.Now += FRAME_WORK;
        const PaceResult result = pacing.EndFrame(false, clock);
        CHECK(result.Outcome == PaceOutcome::Waited);
        CHECK(result.Wake.Nanoseconds == anchor + frame * IDLE_INTERVAL);
    }
    // Presenting again drops the idle schedule; the next idle frame re-anchors instead of
    // honoring a target from before presentation resumed.
    CHECK(pacing.EndFrame(true, clock).Outcome == PaceOutcome::Disabled);
    CHECK(pacing.GetPacer().IsScheduled() == false);
    synthetic.Now += NANOSECONDS_PER_SECOND;
    CHECK(pacing.EndFrame(false, clock).Outcome == PaceOutcome::Baseline);
}
TEST_CASE("A cap paces presented frames and feeds exact frame time to the driver", "[world-demo][pacing]")
{
    LoopPacing pacing;
    REQUIRE(pacing.Configure(30));
    SyntheticClock synthetic;
    const PacingClock clock = MakeClock(synthetic);
    Session session;
    Load(session);
    REQUIRE(pacing.EndFrame(true, clock).Outcome == PaceOutcome::Baseline);
    const uint64 anchor = synthetic.Now;
    // The frame reads its own timestamp after pacing; the first Advance is the driver baseline.
    REQUIRE(session.Driver.Advance(session.World(), {synthetic.Now}, {}) == Status::Success);
    for (uint64 frame = 1; frame <= 30; ++frame)
    {
        synthetic.Now += FRAME_WORK;
        const PaceResult result = pacing.EndFrame(true, clock);
        REQUIRE(result.Outcome == PaceOutcome::Waited);
        REQUIRE(result.Wake.Nanoseconds == anchor + frame * CAP_30_INTERVAL);
        REQUIRE(session.Driver.Advance(session.World(), {synthetic.Now}, {}) == Status::Success);
    }
    // 30 paced frames of 33,333,333 ns accept 999,999,990 ns: 59 ticks plus a remainder just short of
    // the sixtieth. Pacing rounds the cap; the simulation's rational phase keeps the result exact.
    CHECK(session.Driver.GetAcceptedTime() == Duration{30 * CAP_30_INTERVAL});
    CHECK(session.World().GetTick() == 59);
    CHECK(session.Driver.GetDiscardedTicks() == 0);
}
TEST_CASE("Simulation pause and resume do not interrupt presentation pacing", "[world-demo][pacing][lifecycle]")
{
    LoopPacing pacing;
    REQUIRE(pacing.Configure(30));
    SyntheticClock synthetic;
    const PacingClock clock = MakeClock(synthetic);
    Session session;
    Load(session);
    REQUIRE(pacing.EndFrame(true, clock).Outcome == PaceOutcome::Baseline);
    const uint64 anchor = synthetic.Now;
    REQUIRE(session.Driver.Advance(session.World(), {synthetic.Now}, {}) == Status::Success);
    uint64 frame = 0;
    const auto runFrames = [&](uint64 count) {
        for (uint64 index = 0; index < count; ++index)
        {
            ++frame;
            synthetic.Now += FRAME_WORK;
            const PaceResult result = pacing.EndFrame(true, clock);
            REQUIRE(result.Outcome == PaceOutcome::Waited);
            REQUIRE(result.Wake.Nanoseconds == anchor + frame * CAP_30_INTERVAL);
            REQUIRE(session.Driver.Advance(session.World(), {synthetic.Now}, {}) == Status::Success);
        }
    };
    runFrames(3);
    const uint64 tick = session.World().GetTick();
    session.Driver.SetMode(Mode::Paused);
    runFrames(30);
    CHECK(session.World().GetTick() == tick);
    // Resuming drops the driver's baseline, not the presentation schedule.
    session.Driver.SetMode(Mode::Playing);
    runFrames(3);
    CHECK(session.World().GetTick() > tick);
}
TEST_CASE("Session replacement and suspension restart pacing from a baseline", "[world-demo][pacing][lifecycle]")
{
    LoopPacing pacing;
    REQUIRE(pacing.Configure(0));
    SyntheticClock synthetic;
    const PacingClock clock = MakeClock(synthetic);
    Session session;
    Load(session);
    REQUIRE(session.RequestLoad(ExampleLevel()).Error == LevelError::None);
    // Loading frames that present nothing wait at the idle rate instead of polling.
    CHECK(pacing.EndFrame(false, clock).Outcome == PaceOutcome::Baseline);
    uint32 idleFrames = 0;
    while (session.GetLoadStage() != LoadStage::Ready)
    {
        REQUIRE(session.PollLoad() == Status::Success);
        synthetic.Now += FRAME_WORK;
        CHECK(pacing.EndFrame(false, clock).Outcome == PaceOutcome::Waited);
        ++idleFrames;
    }
    CHECK(synthetic.WaitCount == idleFrames);
    // A slow activation frame would otherwise be judged against the loading schedule.
    synthetic.Now += 300 * MILLISECOND;
    REQUIRE(session.Activate());
    pacing.Reset();
    CHECK(pacing.EndFrame(false, clock).Outcome == PaceOutcome::Baseline);

    // Without the reset, the same stall exceeds four intervals and re-anchors without a burst.
    synthetic.Now += 300 * MILLISECOND;
    const PaceResult stalled = pacing.EndFrame(false, clock);
    CHECK(stalled.Outcome == PaceOutcome::Rescheduled);
    CHECK(stalled.WaitCount == 0);
    synthetic.Now += FRAME_WORK;
    CHECK(pacing.EndFrame(false, clock).Outcome == PaceOutcome::Waited);
}
TEST_CASE("The pacing report summarizes wake lateness and intervals", "[world-demo][pacing]")
{
    PacingReport report;
    CHECK(report.Reserve(0) == false);
    CHECK(report.Reserve(MAXIMUM_PACING_RECORDS + 1) == false);
    REQUIRE(report.Reserve(4));
    struct RecordTimes final
    {
        uint64 Wake = 0;
        uint64 Lateness = 0;
        uint64 PresentReturn = 0;
    };
    const auto makeRecord = [](PaceOutcome outcome, const RecordTimes& times) {
        PacingRecord record;
        record.Pace.Outcome = outcome;
        record.Pace.Wake = {EPOCH + times.Wake};
        record.Pace.Lateness = {times.Lateness};
        record.PresentReturn = {EPOCH + times.PresentReturn};
        record.HasPresented = true;
        return record;
    };
    report.Record(makeRecord(PaceOutcome::Baseline, { .Wake = 0, .Lateness = 0, .PresentReturn = 1 * MILLISECOND }));
    report.Record(makeRecord(PaceOutcome::Waited,
                             { .Wake = 10 * MILLISECOND, .Lateness = 100'000, .PresentReturn = 11 * MILLISECOND }));
    report.Record(makeRecord(PaceOutcome::Waited,
                             { .Wake = 20 * MILLISECOND, .Lateness = 300'000, .PresentReturn = 21 * MILLISECOND }));
    report.Record(
        makeRecord(PaceOutcome::Late,
                   { .Wake = 35 * MILLISECOND, .Lateness = 5 * MILLISECOND, .PresentReturn = 36 * MILLISECOND }));
    report.Record(makeRecord(PaceOutcome::Waited,
                             {
                                 .Wake = 40 * MILLISECOND,
                                 .Lateness = 0,
                                 .PresentReturn = 41 * MILLISECOND,
                             }));
    CHECK(report.GetDroppedRecords() == 1);
    PacingSummary summary;
    REQUIRE(report.Summarize(summary));
    CHECK(summary.Frames == 4);
    CHECK(summary.Waited == 2);
    CHECK(summary.Late == 1);
    CHECK(summary.Other == 1);
    CHECK(summary.WakeLateness.Count == 2);
    CHECK(summary.WakeLateness.Minimum == 100'000);
    CHECK(summary.WakeLateness.Median == 100'000);
    CHECK(summary.WakeLateness.Maximum == 300'000);
    CHECK(summary.WakeLateness.Mean == 200'000);
    CHECK(summary.FrameStart.Count == 3);
    CHECK(summary.FrameStart.Median == 10 * MILLISECOND);
    CHECK(summary.FrameStart.Maximum == 15 * MILLISECOND);
    CHECK(summary.PresentReturn.Count == 3);
    CHECK(summary.PresentReturn.Percentile99 == 15 * MILLISECOND);
}
