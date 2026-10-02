#pragma once

// Resident voice DSP kernel, per .kiro/specs/audio/design.md sections 5 and 6.
// Reads immutable prepared PCM through a fractional cursor, handles EOF and
// half-open loops, drives the preinitialized resampler (rate 0.5-2.0 with
// anti-alias filtering), applies persistent gain ramps and equal-power stereo
// panning, and accumulates into a stereo mix buffer. Bounded, allocation-free,
// lock-free, no I/O. Phase/history/cursor persist across partial spans.
//
// The ratio is derived from the session rate and playback Rate (never from
// buffer lengths): the resampler is configured rateIn = sessionRate, rateOut =
// round(sessionRate / Rate), so it consumes ~Rate source frames per output
// frame. Fractional phase lives inside the resampler; the kernel tracks the
// integer source cursor to resolve EOF and loop seams, assembling wrap-aware
// source scratch from immutable PCM.

#include <ludus/audio/audio_types.h>
#include <ludus/foundation/base/types.h>

#include "internal/clip_store.hpp"
#include "internal/resampler.hpp"

namespace ludus::audio::internal
{
using ludus::foundation::float32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;

// Scratch bounds for one voice's per-span input assembly. The largest input
// requirement at Rate 2.0 for a 128-frame span is ~256 source frames plus
// resampler history margin; keep a generous fixed bound.
inline constexpr uint32 VOICE_SCRATCH_FRAMES = 1024;

// Per-voice evolving DSP state the kernel owns (lives in the voice slot). The
// resampler holds fractional phase and filter history; this tracks the integer
// source cursor and the current ramp values.
struct VoiceDsp final
{
    Resampler Rs;            // preinitialized per physical slot
    uint64 SourceCursor = 0; // integer source frame already consumed
    bool AtEof = false;
    uint32 ConfiguredRateOut = 0; // last rateOut set (to detect Rate changes)
};

// Render `frames` output frames for one resident voice into the interleaved
// stereo accumulator `mixLR` (frames*2 samples, += accumulated). `gainL`/`gainR`
// are the target per-channel gains for this span (equal-power pan * attenuation
// * voice gain * bus-chain gain); the kernel ramps from the voice's current
// FadeGain-scaled values across the span. Returns the number of output frames
// actually produced before natural EOF (== frames unless the clip ended and is
// not looping). Updates cursor/eof. Allocation-free (uses the provided scratch).
struct KernelInput final
{
    const ClipSlot* Clip = nullptr;
    VoiceDsp* Dsp = nullptr;
    uint32 SessionRate = 0;
    float32 Rate = 1.0F;
    bool Looping = false;
    uint64 LoopBegin = 0;
    uint64 LoopEnd = 0; // 0 => whole clip, no loop
    // Per-span target gains (already include attenuation/pan/bus/voice gain).
    float32 TargetGainL = 0.0F;
    float32 TargetGainR = 0.0F;
    // Current gains carried across spans for ramping; updated on return.
    float32* CurrentGainL = nullptr;
    float32* CurrentGainR = nullptr;
    // Absolute per-frame ramp step toward the target (sign set by the caller).
    // 0 => jump to target. The step is fixed by the caller when the target
    // changes so a fade completes in a bounded duration.
    float32 StepL = 0.0F;
    float32 StepR = 0.0F;
};

// Returns produced output frames. Writes into mixLR (interleaved stereo, +=).
[[nodiscard]] uint32 RenderResidentVoice(const KernelInput& input,
                                         float32* mixLR,
                                         uint32 frames,
                                         float32* scratch,
                                         uint32 scratchFrames) noexcept;

// Advance a virtual voice's cursor by `frames` output frames WITHOUT reading or
// filtering PCM (design section 7.2). Loops wrap; nonloops set AtEof. The
// fractional cursor remainder is carried in `fraction` across calls so the
// virtual timeline matches the mixed (resampled) timeline and does not drift.
// Returns true if the voice is still alive (not expired at nonloop EOF).
// NOLINTBEGIN(bugprone-easily-swappable-parameters): named transport args.
[[nodiscard]] bool AdvanceVirtualVoice(uint64& cursor,
                                       double& fraction,
                                       uint64 clipFrames,
                                       float32 rate,
                                       bool looping,
                                       uint64 loopBegin,
                                       uint64 loopEnd,
                                       uint32 frames) noexcept;
// NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace ludus::audio::internal
