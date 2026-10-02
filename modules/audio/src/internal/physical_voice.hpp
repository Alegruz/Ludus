#pragma once

// A physical voice slot binds a logical resident voice to a preinitialized DSP
// kernel while it is mixed or fading, per .kiro/specs/audio/design.md sections
// 5-7. The pool has MIXED_VOICE_CAPACITY entries. Each holds one mono and one
// stereo resampler, both preinitialized outside rendering (never created in
// Play/Stop), plus the per-span scratch and the carried gain-ramp values.

#include <ludus/audio/audio_types.h>
#include <ludus/foundation/base/types.h>

#include "internal/voice_kernel.hpp"

namespace ludus::audio::internal
{
using ludus::foundation::float32;
using ludus::foundation::uint32;

struct PhysicalVoice final
{
    bool Bound = false;
    uint32 LogicalSlot = 0; // which logical voice this physical slot serves

    // Preinitialized resamplers for mono and stereo clips at the session rate.
    VoiceDsp Mono{};
    VoiceDsp Stereo{};

    // Carried per-channel gain for ramping across spans.
    float32 CurrentGainL = 0.0F;
    float32 CurrentGainR = 0.0F;

    // Preinitialize both resamplers at the session rate. Returns false on
    // allocation failure. lpfOrder enables anti-alias filtering on downsample.
    [[nodiscard]] bool Init(uint32 sessionRate, uint32 lpfOrder) noexcept
    {
        const bool a = Mono.Rs.Init(1, sessionRate, sessionRate, lpfOrder);
        const bool b = Stereo.Rs.Init(2, sessionRate, sessionRate, lpfOrder);
        return a && b;
    }

    void Uninit() noexcept
    {
        Mono.Rs.Uninit();
        Stereo.Rs.Uninit();
    }

    // Reset DSP transport for a fresh binding (outside rendering).
    void ResetFor(uint32 channels) noexcept
    {
        VoiceDsp& d = channels == 2 ? Stereo : Mono;
        d.Rs.Reset();
        d.SourceCursor = 0;
        d.AtEof = false;
        d.ConfiguredRateOut = 0;
        CurrentGainL = 0.0F;
        CurrentGainR = 0.0F;
    }

    [[nodiscard]] VoiceDsp& DspFor(uint32 channels) noexcept
    {
        return channels == 2 ? Stereo : Mono;
    }
};

} // namespace ludus::audio::internal
