#include "internal/voice_kernel.hpp"

#include <cmath>

namespace ludus::audio::internal
{
namespace
{
// Clamp a float32 to [-1,1] is NOT done here; the kernel accumulates and the bus
// stage clamps once (design section 8). This only computes a bounded ramp step.
[[nodiscard]] float32 RampStep(float32 current, float32 target, uint32 rampFrames) noexcept
{
    if (rampFrames == 0)
    {
        return 0.0F; // jump: handled by setting current=target before the loop
    }
    return (target - current) / static_cast<float32>(rampFrames);
}

// Effective loop end: whole clip when LoopEnd == 0.
[[nodiscard]] uint64 EffectiveLoopEnd(uint64 loopEnd, uint64 clipFrames) noexcept
{
    return loopEnd == 0 ? clipFrames : loopEnd;
}
} // namespace

uint32 RenderResidentVoice(const KernelInput& input,
                           float32* mixLR,
                           uint32 frames,
                           float32* scratch,
                           uint32 scratchFrames) noexcept
{
    const ClipSlot* clip = input.Clip;
    VoiceDsp* dsp = input.Dsp;
    if (clip == nullptr || dsp == nullptr || clip->Pcm == nullptr || frames == 0)
    {
        return 0;
    }
    if (!dsp->Rs.IsReady())
    {
        return 0;
    }

    const uint32 srcChannels = clip->Channels;
    const uint64 clipFrames = clip->Frames;
    const uint64 loopEnd = EffectiveLoopEnd(input.LoopEnd, clipFrames);
    const uint64 loopBegin = input.LoopBegin;

    // Configure the resampler ratio from rates (never buffer lengths). rateOut =
    // sessionRate / Rate so it consumes ~Rate source frames per output frame.
    const uint32 rateOut =
        static_cast<uint32>(std::lround(static_cast<double>(input.SessionRate) / static_cast<double>(input.Rate)));
    if (rateOut != dsp->ConfiguredRateOut)
    {
        if (dsp->Rs.SetRate(input.SessionRate, rateOut == 0 ? input.SessionRate : rateOut))
        {
            dsp->ConfiguredRateOut = rateOut;
        }
    }

    // Ramp setup. On a jump (RampFrames==0) snap current to target first.
    float32 curL = input.CurrentGainL != nullptr ? *input.CurrentGainL : input.TargetGainL;
    float32 curR = input.CurrentGainR != nullptr ? *input.CurrentGainR : input.TargetGainR;
    if (input.RampFrames == 0)
    {
        curL = input.TargetGainL;
        curR = input.TargetGainR;
    }
    const float32 stepL = RampStep(curL, input.TargetGainL, input.RampFrames);
    const float32 stepR = RampStep(curR, input.TargetGainR, input.RampFrames);

    uint32 producedTotal = 0;
    bool endReached = false;

    // Produce output in bounded chunks, assembling wrap-aware source scratch for
    // each chunk and running it through the resampler. The resampler holds the
    // fractional phase/history across chunks and spans.
    while (producedTotal < frames && !endReached)
    {
        uint32 outWant = frames - producedTotal;
        if (outWant > scratchFrames)
        {
            outWant = scratchFrames;
        }

        // How many source frames does this many output frames need at the ratio?
        uint64 needIn = dsp->Rs.RequiredInputFrames(outWant);
        if (needIn == 0)
        {
            needIn = outWant; // conservative fallback
        }
        if (needIn > scratchFrames)
        {
            needIn = scratchFrames;
        }

        // Assemble `needIn` source frames into scratch, honoring loop/EOF. The
        // scratch is interleaved at the clip's channel count.
        uint64 assembled = 0;
        uint64 cursor = dsp->SourceCursor;
        while (assembled < needIn)
        {
            if (cursor >= clipFrames)
            {
                if (input.Looping && loopEnd > loopBegin)
                {
                    cursor = loopBegin;
                }
                else
                {
                    endReached = true;
                    break;
                }
            }
            // When looping, wrap at loopEnd (half-open), not clip end.
            uint64 regionEnd = clipFrames;
            if (input.Looping && loopEnd > loopBegin)
            {
                regionEnd = loopEnd;
                if (cursor >= loopEnd)
                {
                    cursor = loopBegin;
                }
            }
            uint64 avail = regionEnd - cursor;
            uint64 take = needIn - assembled;
            if (take > avail)
            {
                take = avail;
            }
            const float32* src = clip->Pcm + cursor * srcChannels;
            float32* dst = scratch + assembled * srcChannels;
            for (uint64 i = 0; i < take * srcChannels; ++i)
            {
                dst[i] = src[i];
            }
            assembled += take;
            cursor += take;
        }

        if (assembled == 0)
        {
            endReached = true;
            break;
        }

        // Run the resampler: consume up to `assembled` input, produce up to
        // `outWant` output. The resampler reports exact consumed/produced.
        uint64 inCount = assembled;
        uint64 outCount = outWant;
        // Temporary stereo-or-mono output into the tail of scratch is avoided;
        // produce directly into a small stereo staging area reused per chunk.
        // We resample into the same scratch region shifted past the input.
        float32* outStage = scratch + static_cast<uint64>(scratchFrames) * srcChannels;
        // outStage must hold outWant * srcChannels; the scratch buffer the caller
        // supplies is sized for both (2x). Guard conservatively.
        if (!dsp->Rs.Process(scratch, &inCount, outStage, &outCount))
        {
            break;
        }

        // Advance the integer source cursor by the frames the resampler consumed.
        dsp->SourceCursor += inCount;
        // Normalize cursor against loop region for the next chunk.
        if (input.Looping && loopEnd > loopBegin)
        {
            while (dsp->SourceCursor >= loopEnd)
            {
                dsp->SourceCursor = loopBegin + (dsp->SourceCursor - loopEnd);
            }
        }

        if (outCount == 0)
        {
            // No forward progress possible (ran out of input at EOF).
            if (endReached)
            {
                break;
            }
            // Avoid an infinite loop: treat as end.
            endReached = true;
            break;
        }

        // Mix the produced output into the stereo accumulator with the ramp.
        for (uint64 f = 0; f < outCount; ++f)
        {
            float32 l = 0.0F;
            float32 r = 0.0F;
            if (srcChannels == 1)
            {
                const float32 s = outStage[f];
                l = s;
                r = s;
            }
            else
            {
                l = outStage[f * 2 + 0];
                r = outStage[f * 2 + 1];
            }
            const uint32 outFrame = producedTotal + static_cast<uint32>(f);
            mixLR[outFrame * 2 + 0] += l * curL;
            mixLR[outFrame * 2 + 1] += r * curR;
            curL += stepL;
            curR += stepR;
            // Clamp ramp overshoot at the target.
            if ((stepL > 0.0F && curL > input.TargetGainL) || (stepL < 0.0F && curL < input.TargetGainL))
            {
                curL = input.TargetGainL;
            }
            if ((stepR > 0.0F && curR > input.TargetGainR) || (stepR < 0.0F && curR < input.TargetGainR))
            {
                curR = input.TargetGainR;
            }
        }
        producedTotal += static_cast<uint32>(outCount);
    }

    if (input.CurrentGainL != nullptr)
    {
        *input.CurrentGainL = curL;
    }
    if (input.CurrentGainR != nullptr)
    {
        *input.CurrentGainR = curR;
    }
    if (endReached && !input.Looping)
    {
        dsp->AtEof = true;
    }
    return producedTotal;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters): named transport args.
bool AdvanceVirtualVoice(VoiceDsp& dsp,
                         uint64 clipFrames,
                         float32 rate,
                         bool looping,
                         uint64 loopBegin,
                         uint64 loopEnd,
                         uint32 frames) noexcept
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const uint64 end = EffectiveLoopEnd(loopEnd, clipFrames);
    // Advance the integer cursor by rate * frames source frames (fractional part
    // tracked coarsely; virtual advance does not read PCM so exact subsample
    // phase is irrelevant until reentry, which rebuilds history around the
    // integer cursor).
    const double advance = static_cast<double>(rate) * static_cast<double>(frames);
    uint64 whole = static_cast<uint64>(advance);
    dsp.SourceCursor += whole;

    if (looping && end > loopBegin)
    {
        while (dsp.SourceCursor >= end)
        {
            dsp.SourceCursor = loopBegin + (dsp.SourceCursor - end);
        }
        return true; // looping voices never expire from advance
    }
    if (dsp.SourceCursor >= clipFrames)
    {
        dsp.SourceCursor = clipFrames;
        dsp.AtEof = true;
        return false; // finite nonloop voice expired
    }
    return true;
}

} // namespace ludus::audio::internal
