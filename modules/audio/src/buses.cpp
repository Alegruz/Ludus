#include <ludus/audio/audio_system.h>

#include "internal/impl.hpp"

#include <cmath>

// Static bus-tree accumulation and metering, per .kiro/specs/audio/design.md
// section 8. Each bus gain is applied exactly once at its edge: a child folds
// its post-gain output into its parent's input accumulator, so an ancestor gain
// is never multiplied into a voice twice. Processing is child-before-parent
// (buses were validated so a parent index is always smaller, giving a safe
// reverse-index fold). Per-bus input and post-gain peak/RMS meters use a fixed
// window; pre-clamp overs are counted at the root.

namespace ludus::audio
{
uint32 AudioSystem::Impl::RootBusIndex() const noexcept
{
    for (uint32 b = 0; b < BusCount; ++b)
    {
        if (Buses[b].ParentIndex == kNoParent)
        {
            return b;
        }
    }
    return 0;
}

void AudioSystem::Impl::BeginBusSpan(uint32 span) noexcept
{
    for (uint32 b = 0; b < BusCount; ++b)
    {
        float32* base = BusAccumArena + static_cast<uint64>(b) * internal::VOICE_SCRATCH_FRAMES * 2;
        Buses[b].AccumL = base;
        Buses[b].AccumR = base + internal::VOICE_SCRATCH_FRAMES;
        for (uint32 f = 0; f < span; ++f)
        {
            Buses[b].AccumL[f] = 0.0F;
            Buses[b].AccumR[f] = 0.0F;
        }
    }
}

void AudioSystem::Impl::RouteVoiceToBus(uint32 busIndex, const float32* mixLR, uint32 span) noexcept
{
    if (busIndex >= BusCount)
    {
        return;
    }
    float32* l = Buses[busIndex].AccumL;
    float32* r = Buses[busIndex].AccumR;
    for (uint32 f = 0; f < span; ++f)
    {
        l[f] += mixLR[f * 2 + 0];
        r[f] += mixLR[f * 2 + 1];
    }
}

void AudioSystem::Impl::FoldBusesToRoot(uint32 span) noexcept
{
    // Child-before-parent: process from the highest index down to 0. A nonroot
    // bus's parent always has a smaller index (validated), so folding in
    // descending index order guarantees every child is finalized before its
    // parent is folded. Meter the input tap (before this bus's gain), apply the
    // bus's own ramped effective gain once, meter the post-gain tap, then add the
    // post-gain signal into the parent's input accumulator.
    for (int32 bi = static_cast<int32>(BusCount) - 1; bi >= 0; --bi)
    {
        const uint32 b = static_cast<uint32>(bi);
        internal::BusState& bus = Buses[b];
        const float32 gain = bus.CurrentEffective;

        for (uint32 f = 0; f < span; ++f)
        {
            const float32 inL = bus.AccumL[f];
            const float32 inR = bus.AccumR[f];
            bus.InputMeter.Accumulate(inL, inR);

            const float32 postL = inL * gain;
            const float32 postR = inR * gain;
            bus.PostMeter.Accumulate(postL, postR);

            // Overwrite the accumulator with post-gain so the parent reads it.
            bus.AccumL[f] = postL;
            bus.AccumR[f] = postR;
        }
        bus.InputMeter.MaybePublish(MeterWindowFrames);
        bus.PostMeter.MaybePublish(MeterWindowFrames);

        if (bus.ParentIndex != kNoParent && bus.ParentIndex < BusCount)
        {
            internal::BusState& parent = Buses[bus.ParentIndex];
            for (uint32 f = 0; f < span; ++f)
            {
                parent.AccumL[f] += bus.AccumL[f];
                parent.AccumR[f] += bus.AccumR[f];
            }
        }
    }
}

} // namespace ludus::audio
