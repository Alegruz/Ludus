#include <ludus/audio/audio_gym.h>

#include <utility>

namespace ludus::audio::gym
{
namespace
{
using ludus::foundation::Array;

// Copies matching voices from the most recent capture into staging, then swaps
// it into the output so allocation failure leaves outVoices unchanged.
template <typename MatchFunction>
[[nodiscard]] Status
FilterLatest(const Array<Capture>& captures, const MatchFunction& isMatch, Array<VoiceInfo>& outVoices) noexcept
{
    Array<VoiceInfo> matches;
    if (captures.IsEmpty() == false)
    {
        for (const VoiceInfo& voice : captures.GetLast().Voices)
        {
            if (isMatch(voice) == true && matches.TryAdd(voice) == false)
            {
                return Status::OutOfMemory;
            }
        }
    }
    outVoices.Swap(matches);
    return Status::Ok;
}
} // namespace

Scenario::Scenario(std::string name, const SystemConfig& config) noexcept : mName(std::move(name))
{
    (void)mSystem.Initialize(config);
}

Scenario::~Scenario() noexcept = default;

Status Scenario::PrepareClip(std::span<const uint8> encoded, const ClipDescriptor& descriptor, ClipHandle& out) noexcept
{
    return mSystem.PrepareClip(encoded, descriptor, out);
}

Status Scenario::Track(VoiceHandle voice) noexcept
{
    return mTracked.TryAdd(voice) == true ? Status::Ok : Status::OutOfMemory;
}

Status Scenario::Advance(uint32 frames) noexcept
{
    // Allocate all gym storage before rendering so OutOfMemory changes nothing
    // (cold gym allocations, outside the renderer).
    const usize needed = static_cast<usize>(frames) * 2;
    if (mScratch.GetSize() < needed && mScratch.TryResize(needed) == false)
    {
        return Status::OutOfMemory;
    }
    Capture capture{};
    if (capture.Voices.TryEnsureCapacity(mTracked.GetSize()) == false ||
        mCaptures.TryEnsureCapacity(mCaptures.GetSize() + 1) == false)
    {
        return Status::OutOfMemory;
    }
    const Status rendered = mSystem.RenderOffline(std::span<float32>(mScratch.GetData(), needed),
                                                  ChannelLayout::Stereo,
                                                  BufferLayout::Interleaved,
                                                  frames);
    mSystem.Service();

    mSystem.GetSystemSnapshot(capture.System);
    capture.RenderFrame = capture.System.RenderFrame;
    for (const VoiceHandle voice : mTracked)
    {
        VoiceInfo info{};
        if (IsOk(mSystem.GetVoiceInfo(voice, info)) == true)
        {
            capture.Voices.Add(info); // within the reserved capacity
        }
    }
    mCaptures.Add(std::move(capture)); // within the reserved capacity
    return rendered;
}

Status Scenario::FilterByGroup(uint32 groupIndex, Array<VoiceInfo>& outVoices) const noexcept
{
    return FilterLatest(
        mCaptures,
        [groupIndex](const VoiceInfo& voice) { return voice.GroupIndex == groupIndex; },
        outVoices);
}

Status Scenario::FilterByBus(uint32 busIndex, Array<VoiceInfo>& outVoices) const noexcept
{
    return FilterLatest(
        mCaptures,
        [busIndex](const VoiceInfo& voice) { return voice.BusIndex == busIndex; },
        outVoices);
}

Status Scenario::FilterByTag(uint32 policyTag, Array<VoiceInfo>& outVoices) const noexcept
{
    return FilterLatest(
        mCaptures,
        [policyTag](const VoiceInfo& voice) { return voice.PolicyTag == policyTag; },
        outVoices);
}

} // namespace ludus::audio::gym
