#pragma once

// Renderer-owned mixer state and the bounded render entry, per
// .kiro/specs/audio/design.md sections 5-8. The mixer owns live voice cursors,
// DSP state and bus gains. It is driven at 128-frame control boundaries from a
// single control producer's command batches, consumed from the SPSC ring. It
// allocates/frees nothing on the warm path, takes no lock, performs no I/O and
// invokes no gameplay callback.
//
// A1 implements: fixed slot storage, the explicit voice state machine with
// numeric causes, resident-group transactional MaxAdmitted charges, scheduled
// starts at exact sample offsets, StopGeneration + StopAll epoch mailboxes,
// durable terminal mailboxes, and the bus EffectiveGain composition. The warm
// sample output is correct but silent (no PCM read) until A2 installs the
// resident DSP/resampler and A4 the streams. Everything bookkeeping-level is
// production code exercised by the offline gym and tests.

#include <ludus/audio/audio_types.h>
#include <ludus/foundation/base/types.h>

#include "internal/clip_store.hpp"
#include "internal/handles.hpp"

#include <atomic>
#include <cmath>
#include <span>

namespace ludus::audio::internal
{
using ludus::foundation::float32;
using ludus::foundation::int32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;
using ludus::foundation::usize;

// A per-slot durable terminal mailbox (design section 4). The renderer writes
// Reason/FinalFrame then release-publishes TerminalGeneration; the owner reads
// the payload only after acquiring the matching generation. This is the
// authority for releasing asset pins, independent of any lossy debug ring.
struct TerminalMailbox final
{
    std::atomic<uint32> TerminalGeneration{0};
    TerminalReason Reason = TerminalReason::None;
    uint64 FinalFrame = 0;
};

// A per-slot stop mailbox (design section 4). Stop stores the desired generation
// with release ordering; the renderer acquires it at each boundary and when
// installing a queued play.
struct StopMailbox final
{
    std::atomic<uint32> StopGeneration{0};
};

// One resident logical/physical voice slot.
struct VoiceSlot final
{
    SlotGeneration Generation{};
    bool InUse = false;

    VoiceState State = VoiceState::Terminal;
    StateCause LastCause = StateCause::None;

    // Immutable-once admission parameters (never re-rolled on reentry).
    uint32 ClipSlotIndex = 0;
    uint32 BusIndex = 0;
    uint32 GroupIndex = 0;
    uint8 Priority = 0;
    float32 Gain = 1.0F; // current/target voice gain
    float32 Rate = 1.0F; // playback rate [0.5,2.0]
    bool Looping = false;
    VirtualPolicy Policy = VirtualPolicy::KillWhenInaudible;
    bool Positional = false;
    AttenuationOrigin Origin = AttenuationOrigin::Listener;
    float32 MinDistance = 1.0F;
    float32 MaxDistance = 100.0F;
    float32 ExplicitPan = 0.0F;
    AudioVec3 EmitterPosition{};
    uint32 PolicyTag = 0;
    bool IsStream = false;
    uint32 StreamSlotIndex = 0;

    // Scheduling.
    uint64 StartFrame = 0; // absolute session frame; 0 => next boundary
    bool LateStart = false;
    uint64 Lateness = 0;

    // Evolving transport (prepared-rate fractional cursor; A2 uses the fraction).
    uint64 CursorFrame = 0;
    float64 CursorFraction = 0.0;

    // Admission epoch (for StopAll). A play whose epoch is older than the live
    // StopAll epoch is cancelled even if still queued.
    uint32 AdmissionEpoch = 0;

    // Fade bookkeeping (section 6/7). FadeGain ramps toward FadeTarget.
    float32 FadeGain = 0.0F;
    float32 FadeTarget = 1.0F;

    // Physical binding (MIXED_VOICE_CAPACITY == unbound).
    uint32 PhysicalIndex = 0xFFFFFFFFU;

    // Per-boundary target per-channel gains (pan*attenuation*voice*bus*fade).
    float32 TargetGainL = 0.0F;
    float32 TargetGainR = 0.0F;

    // Virtual bookkeeping.
    uint64 VirtualSinceFrame = 0;

    // Prepared whole-clip peak captured at admission (conservative audibility
    // estimate; design 7.2). Stored once; never re-scanned in rendering.
    float32 PreparedPeakScore = 0.0F;

    // Selection score contributions (section 7.2), recomputed per boundary.
    float32 ScoreVoiceGain = 0.0F;
    float32 ScorePreparedPeak = 0.0F;
    float32 ScoreDistanceGain = 0.0F;
    float32 ScoreBusChainGain = 0.0F;
    float32 Pan = 0.0F;
    SilenceCause Silence = SilenceCause::None;

    // Pending-play reference: while Pending, the queued Play still references the
    // clip pin; a Stop on Pending must wait for the play to be drained.
    bool PlayDrained = false;

    StopMailbox Stop{};
    TerminalMailbox Terminal{};
};

// A physical voice slot: a preinitialized DSP kernel (mono + stereo resamplers)
// bound to a logical voice while it is mixed/fading (design section 7). The
// pool size is the mixed-voice budget. Resamplers are initialized once outside
// rendering and never re-created in Play/Stop.
struct PhysicalVoice; // defined in physical_voice.hpp (needs voice_kernel.hpp)

// Resident concurrency group with transactional MaxAdmitted / deterministic
// MaxSelected quotas (design section 7.1).
struct GroupState final
{
    uint32 MaxAdmitted = GROUP_MAX_ADMITTED_LIMIT;
    uint32 MaxSelected = GROUP_MAX_SELECTED_LIMIT;
    uint32 Admitted = 0; // charged logical reservations (incl. unreclaimed terminal)
    uint32 Selected = 0; // steady-state selected this boundary
    uint32 Fading = 0;   // Virtualizing/Stopping tails
};

// Per-bus metering accumulator over a fixed window (design section 8). Peaks
// are max-abs; RMS is sqrt(mean-square) over the window. Windows reset when a
// full window of frames has accumulated so meters track recent content.
struct MeterAccum final
{
    float32 PeakL = 0.0F;
    float32 PeakR = 0.0F;
    float64 SumSqL = 0.0;
    float64 SumSqR = 0.0;
    uint32 Frames = 0;
    // Published values (updated at window boundaries).
    float32 OutPeakL = 0.0F;
    float32 OutPeakR = 0.0F;
    float32 OutRmsL = 0.0F;
    float32 OutRmsR = 0.0F;

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): L/R channel samples.
    void Accumulate(float32 l, float32 r) noexcept
    {
        const float32 al = l < 0.0F ? -l : l;
        const float32 ar = r < 0.0F ? -r : r;
        if (al > PeakL)
        {
            PeakL = al;
        }
        if (ar > PeakR)
        {
            PeakR = ar;
        }
        SumSqL += static_cast<float64>(l) * static_cast<float64>(l);
        SumSqR += static_cast<float64>(r) * static_cast<float64>(r);
        ++Frames;
    }

    void MaybePublish(uint32 windowFrames) noexcept
    {
        if (Frames < windowFrames)
        {
            return;
        }
        OutPeakL = PeakL;
        OutPeakR = PeakR;
        OutRmsL = Frames > 0 ? static_cast<float32>(std::sqrt(SumSqL / Frames)) : 0.0F;
        OutRmsR = Frames > 0 ? static_cast<float32>(std::sqrt(SumSqR / Frames)) : 0.0F;
        PeakL = 0.0F;
        PeakR = 0.0F;
        SumSqL = 0.0;
        SumSqR = 0.0;
        Frames = 0;
    }
};

// Bus node in the static tree with user/base gain and ramped effective gain
// (design section 8).
struct BusState final
{
    uint32 Id = 0;
    uint32 ParentIndex = kNoParent;
    uint32 Depth = 0;
    float32 UserGain = 1.0F;
    float32 BaseGain = 1.0F;
    float32 TargetBaseGain = 1.0F;
    float32 CurrentEffective = 1.0F; // this bus's own ramped gain (not chain)
    float32 TargetEffective = 1.0F;

    // Per-bus stereo accumulator for the current span (input, before this bus's
    // gain). Children add their post-gain output here; direct voices add here.
    float32* AccumL = nullptr; // points into the Impl's shared bus-accum arena
    float32* AccumR = nullptr;

    // Input and post-gain metering taps.
    MeterAccum InputMeter{};
    MeterAccum PostMeter{};
    uint32 ClippedFrames = 0;
};

} // namespace ludus::audio::internal
