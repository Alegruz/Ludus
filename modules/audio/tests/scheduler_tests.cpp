// Incremental cold-worker decode-scheduler tests (design section 9; vol 1 ch.3 /
// vol 3 ch.9). These exercise the deadline-first incremental discipline through
// the production scheduler with injected decode sources -- deterministic, no
// device, no OS thread. The full stream rings/device consumption remain A4.

#include "internal/decode_scheduler.hpp"

#include <ludus/audio/audio_types.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace ludus::audio;
using ludus::audio::internal::DecodeScheduler;
using ludus::audio::internal::DecodeSource;
using ludus::audio::internal::SchedulerStats;

namespace
{
// A source that produces `total` frames, then EOF, counting Decode calls and the
// largest single-unit request it saw.
class CountingSource final : public DecodeSource
{
public:
    explicit CountingSource(uint64 total) noexcept : mRemaining(total) {}

    uint32 Decode(float32* out, uint32 maxFrames, bool& eof) noexcept override
    {
        ++Calls;
        if (maxFrames > MaxUnit)
        {
            MaxUnit = maxFrames;
        }
        uint32 n = maxFrames;
        if (static_cast<uint64>(n) >= mRemaining)
        {
            n = static_cast<uint32>(mRemaining);
            eof = true;
        }
        for (uint32 i = 0; i < n * 2; ++i)
        {
            out[i] = 0.0F;
        }
        mRemaining -= n;
        return n;
    }

    uint64 mRemaining;
    uint32 Calls = 0;
    uint32 MaxUnit = 0;
};
} // namespace

TEST_CASE("Cold decode runs in units of at most 4096 prepared frames", "[audio][worker]")
{
    DecodeScheduler sched;
    sched.Configure(48000);
    CountingSource src(100000); // ~24 units of 4096
    REQUIRE(sched.BeginColdPrepare(&src, 100000));

    std::vector<float32> scratch(static_cast<std::size_t>(COLD_DECODE_UNIT_FRAMES) * 2);
    uint32 passes = 0;
    while (sched.ColdPrepareActive() && passes < 100)
    {
        (void)sched.RunPass(scratch.data(), COLD_DECODE_UNIT_FRAMES, 4);
        ++passes;
    }
    REQUIRE_FALSE(sched.ColdPrepareActive());
    REQUIRE(sched.ColdPrepareProduced() == 100000);
    // No single decode unit exceeded the 4096-frame cap.
    REQUIRE(src.MaxUnit <= COLD_DECODE_UNIT_FRAMES);
    // It took multiple units (incremental), not one monolithic decode.
    REQUIRE(src.Calls >= 100000 / COLD_DECODE_UNIT_FRAMES);
}

TEST_CASE("A long cold prepare never starves an established stream", "[audio][worker]")
{
    DecodeScheduler sched;
    sched.Configure(48000);

    // An active stream that is below its refill target (starved).
    CountingSource streamSrc(1000000);
    const uint32 slot = sched.AddStream(&streamSrc, 24576); // 6 chunks
    REQUIRE(slot < internal::SCHEDULER_STREAM_CAPACITY);
    sched.Stream(slot)->Buffered = 0; // fully starved

    // A huge cold prepare competing for the worker.
    CountingSource coldSrc(1000000);
    REQUIRE(sched.BeginColdPrepare(&coldSrc, 1000000));

    std::vector<float32> scratch(static_cast<std::size_t>(COLD_DECODE_UNIT_FRAMES) * 2);
    // One bounded pass: the starved stream must be serviced before the cold job.
    const SchedulerStats s = sched.RunPass(scratch.data(), COLD_DECODE_UNIT_FRAMES, 1);
    REQUIRE(s.UnitsDecoded == 1);
    // The stream's buffer grew; the cold job did not run while a stream starved.
    REQUIRE(sched.Stream(slot)->Buffered > 0);
    REQUIRE(sched.ColdPrepareProduced() == 0);
}

TEST_CASE("Deadline-first refill services the most-starved stream, round-robin ties", "[audio][worker]")
{
    DecodeScheduler sched;
    sched.Configure(48000);
    CountingSource a(1000000);
    CountingSource b(1000000);
    const uint32 sa = sched.AddStream(&a, 20000);
    const uint32 sb = sched.AddStream(&b, 20000);
    // b is more starved than a -> b served first.
    sched.Stream(sa)->Buffered = 8000;
    sched.Stream(sb)->Buffered = 2000;

    std::vector<float32> scratch(static_cast<std::size_t>(COLD_DECODE_UNIT_FRAMES) * 2);
    const uint64 bBefore = sched.Stream(sb)->Buffered;
    const uint64 aBefore = sched.Stream(sa)->Buffered;
    (void)sched.RunPass(scratch.data(), COLD_DECODE_UNIT_FRAMES, 1);
    // The most-starved (b) got the first (only) unit this pass.
    REQUIRE(sched.Stream(sb)->Buffered > bBefore);
    REQUIRE(sched.Stream(sa)->Buffered == aBefore);

    // Equal deadlines: both at the same buffered level -> round-robin alternates.
    sched.Stream(sa)->Buffered = 5000;
    sched.Stream(sb)->Buffered = 5000;
    const uint64 a0 = sched.Stream(sa)->Buffered;
    const uint64 b0 = sched.Stream(sb)->Buffered;
    (void)sched.RunPass(scratch.data(), COLD_DECODE_UNIT_FRAMES, 1);
    (void)sched.RunPass(scratch.data(), COLD_DECODE_UNIT_FRAMES, 1);
    // Over two single-unit passes with equal deadlines, both advanced (fairness).
    REQUIRE(sched.Stream(sa)->Buffered > a0);
    REQUIRE(sched.Stream(sb)->Buffered > b0);
}

TEST_CASE("Cancellation is honored between units and retires the stream", "[audio][worker]")
{
    DecodeScheduler sched;
    sched.Configure(48000);
    CountingSource src(1000000);
    const uint32 slot = sched.AddStream(&src, 20000);
    sched.Stream(slot)->Buffered = 0;
    sched.CancelStream(slot);

    std::vector<float32> scratch(static_cast<std::size_t>(COLD_DECODE_UNIT_FRAMES) * 2);
    const SchedulerStats s = sched.RunPass(scratch.data(), COLD_DECODE_UNIT_FRAMES, 2);
    REQUIRE(s.CancellationsHonored >= 1);
    // The cancelled stream was retired before any further decode for it.
    REQUIRE_FALSE(sched.Stream(slot)->Active);
}

TEST_CASE("Scheduler reports buffered runway and bounds the pass", "[audio][worker]")
{
    DecodeScheduler sched;
    sched.Configure(48000);
    CountingSource src(1000000);
    const uint32 slot = sched.AddStream(&src, 20000);
    sched.Stream(slot)->Buffered = 1234;

    std::vector<float32> scratch(static_cast<std::size_t>(COLD_DECODE_UNIT_FRAMES) * 2);
    const SchedulerStats s = sched.RunPass(scratch.data(), COLD_DECODE_UNIT_FRAMES, 3);
    REQUIRE(s.UnitsDecoded <= 3);             // pass bounded by budgetUnits
    REQUIRE(s.YieldPoints >= s.UnitsDecoded); // a yield recheck precedes each unit
    REQUIRE(s.MinBufferedRunway <= 1234);     // observed the (low) runway
}
