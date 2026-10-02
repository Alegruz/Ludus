#include <ludus/audio/audio_gym.h>

#include <utility>

namespace ludus::audio::gym
{
Scenario::Scenario(std::string name, const SystemConfig& config) noexcept : mName(std::move(name))
{
    (void)mSystem.Initialize(config);
}

Scenario::~Scenario() noexcept = default;

Status Scenario::PrepareClip(std::span<const uint8> encoded, const ClipDescriptor& descriptor, ClipHandle& out) noexcept
{
    return mSystem.PrepareClip(encoded, descriptor, out);
}

void Scenario::Track(VoiceHandle voice) noexcept
{
    mTracked.push_back(voice);
}

Status Scenario::Advance(uint32 frames) noexcept
{
    const std::size_t needed = static_cast<std::size_t>(frames) * 2;
    if (mScratch.size() < needed)
    {
        mScratch.resize(needed); // cold gym allocation (outside the renderer)
    }
    const Status r = mSystem.RenderOffline(std::span<float32>(mScratch.data(), needed),
                                           ChannelLayout::Stereo,
                                           BufferLayout::Interleaved,
                                           frames);
    mSystem.Service();

    Capture cap{};
    mSystem.GetSystemSnapshot(cap.System);
    cap.RenderFrame = cap.System.RenderFrame;
    for (VoiceHandle v : mTracked)
    {
        VoiceInfo info{};
        if (IsOk(mSystem.GetVoiceInfo(v, info)))
        {
            cap.Voices.push_back(info);
        }
    }
    mCaptures.push_back(std::move(cap));
    return r;
}

std::vector<VoiceInfo> Scenario::FilterByGroup(uint32 groupIndex) const noexcept
{
    std::vector<VoiceInfo> out;
    if (mCaptures.empty())
    {
        return out;
    }
    for (const VoiceInfo& v : mCaptures.back().Voices)
    {
        if (v.GroupIndex == groupIndex)
        {
            out.push_back(v);
        }
    }
    return out;
}

std::vector<VoiceInfo> Scenario::FilterByBus(uint32 busIndex) const noexcept
{
    std::vector<VoiceInfo> out;
    if (mCaptures.empty())
    {
        return out;
    }
    for (const VoiceInfo& v : mCaptures.back().Voices)
    {
        if (v.BusIndex == busIndex)
        {
            out.push_back(v);
        }
    }
    return out;
}

std::vector<VoiceInfo> Scenario::FilterByTag(uint32 policyTag) const noexcept
{
    std::vector<VoiceInfo> out;
    if (mCaptures.empty())
    {
        return out;
    }
    for (const VoiceInfo& v : mCaptures.back().Voices)
    {
        if (v.PolicyTag == policyTag)
        {
            out.push_back(v);
        }
    }
    return out;
}

} // namespace ludus::audio::gym
