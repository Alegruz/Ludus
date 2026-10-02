#pragma once

// Incremental cold-worker decode scheduler, per .kiro/specs/audio/design.md
// section 9 and the Game Audio Programming vol 1 ch.3 / vol 3 ch.9 follow-ups.
//
// This is the deadline-first incremental decode DISCIPLINE, independent of the
// (A4-pending) stream rings, device consumption and OS thread. It is pure,
// bounded and deterministically testable offline against an injected decode
// source: a cold whole-clip prepare is split into units of at most
// COLD_DECODE_UNIT_FRAMES prepared output frames, and BETWEEN units the
// scheduler rechecks active streams (ordered by time-to-empty = buffered frames
// / session rate), services the most-starved, breaks equal deadlines
// round-robin, and honours cancellation. It never lets one long cold prepare
// starve an established stream. It measures per-unit decode time and the minimum
// buffered runway rather than claiming hard real-time preemption.
//
// No miniaudio type appears here; the source is an abstract decoder the worker
// (A4) or a test supplies. The scheduler allocates nothing on the hot path.

#include <ludus/audio/audio_types.h>
#include <ludus/foundation/base/types.h>

namespace ludus::audio::internal
{
using ludus::foundation::float32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::usize;

// Abstract cold decode source. Implementations decode up to `maxFrames` output
// frames (session rate, stereo) into `out`, returning the count produced and
// whether EOF was reached. A blocking reader/codec may exceed its deadline; the
// scheduler measures time but does not claim preemption. Decoding is cold and
// off the render path.
class DecodeSource
{
public:
    virtual ~DecodeSource() = default;

    // Decode at most maxFrames stereo frames into out (maxFrames*2 samples).
    // Returns frames produced; sets eof when the source is exhausted. Returns 0
    // with eof=false only on a transient with no data yet (treated as 0 runway).
    [[nodiscard]] virtual uint32 Decode(float32* out, uint32 maxFrames, bool& eof) noexcept = 0;
};

// One scheduled stream instance the worker is refilling. Buffered is the number
// of decoded-but-unconsumed frames currently in its ring; the renderer lowers it
// by consuming. RefillTarget is the number of frames to top up toward.
struct ScheduledStream final
{
    bool Active = false;
    bool Cancelled = false;
    bool Eof = false;
    DecodeSource* Source = nullptr;
    uint64 Buffered = 0;        // decoded frames available (time-to-empty numerator)
    uint64 RefillTarget = 0;    // decode up toward this many buffered frames
    uint64 Produced = 0;        // total frames decoded this session (diagnostics)
    uint32 LastServiceRank = 0; // round-robin tiebreaker (lower served earlier)
};

// Per-run measurements (design section 13: measure, do not claim preemption).
struct SchedulerStats final
{
    uint32 UnitsDecoded = 0;      // cold units run this pass
    uint32 YieldPoints = 0;       // deadline rechecks between units
    uint64 FramesDecoded = 0;     // total output frames produced
    uint64 MinBufferedRunway = 0; // lowest observed buffered frames across actives
    uint32 CancellationsHonored = 0;
};

inline constexpr usize SCHEDULER_STREAM_CAPACITY = STREAM_CAPACITY;

// The scheduler owns a fixed set of stream slots plus one "cold prepare" job
// that competes with refills. One owner/worker thread drives it; it holds no
// lock and allocates nothing after construction.
class DecodeScheduler final
{
public:
    DecodeScheduler() noexcept = default;

    DecodeScheduler(const DecodeScheduler&) = delete;
    DecodeScheduler& operator=(const DecodeScheduler&) = delete;

    void Configure(uint32 sessionRate) noexcept
    {
        mSessionRate = sessionRate == 0 ? DEFAULT_SAMPLE_RATE : sessionRate;
    }

    // Register/clear a stream refill slot. Returns the slot index or capacity on
    // none free.
    [[nodiscard]] uint32 AddStream(DecodeSource* source, uint64 refillTarget) noexcept;
    void CancelStream(uint32 slot) noexcept;
    [[nodiscard]] ScheduledStream* Stream(uint32 slot) noexcept;

    // Renderer feedback: a stream consumed `frames` from its ring (lowers the
    // buffered runway). Called off the render path by the owner draining
    // consumption counters.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): named slot/frames.
    void NotifyConsumed(uint32 slot, uint64 frames) noexcept;

    // Run one worker pass: service the cold prepare job (if any) and refills in
    // deadline-first order, decoding in units of at most COLD_DECODE_UNIT_FRAMES
    // and rechecking between units. `scratch` holds COLD_DECODE_UNIT_FRAMES*2
    // samples. `budgetUnits` bounds the pass so the worker returns to recheck
    // cancellation/new jobs. Returns the stats for the pass.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): named frame counts.
    [[nodiscard]] SchedulerStats RunPass(float32* scratch, uint32 scratchFrames, uint32 budgetUnits) noexcept;

    // Install a cold whole-source prepare that must not starve active refills.
    // It is decoded incrementally, interleaved with refills by deadline.
    [[nodiscard]] bool BeginColdPrepare(DecodeSource* source, uint64 totalTargetFrames) noexcept;
    [[nodiscard]] bool ColdPrepareActive() const noexcept
    {
        return mCold.Active;
    }
    [[nodiscard]] uint64 ColdPrepareProduced() const noexcept
    {
        return mCold.Produced;
    }

private:
    // Pick the active refill stream with the least time-to-empty (buffered /
    // rate); break equal deadlines round-robin via LastServiceRank. Returns the
    // slot or capacity if none needs refill.
    [[nodiscard]] uint32 PickMostStarved() noexcept;

    uint32 mSessionRate = DEFAULT_SAMPLE_RATE;
    ScheduledStream mStreams[SCHEDULER_STREAM_CAPACITY]{};
    ScheduledStream mCold{}; // the cold prepare job (uses the same unit discipline)
    uint32 mRoundRobin = 0;
};

} // namespace ludus::audio::internal
