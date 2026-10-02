#pragma once

// A small offline audio gym, per .kiro/specs/audio/design.md section 11 and
// requirement AU16. It assembles named, reproducible offline scenarios against
// the production AudioSystem, captures owner-side snapshots/voice info, and
// supports filtering by voice/tag/group/bus. It is owner-side only: no device,
// no worker, no UI dependency, no callback-time work. The gym is a thin test/
// demonstration harness built on the public API; it adds no engine policy.

#include <ludus/audio/audio_debug.h>
#include <ludus/audio/audio_system.h>
#include <ludus/audio/audio_types.h>
#include <ludus/foundation/base/types.h>

#include <span>
#include <string>
#include <vector>

namespace ludus::audio::gym
{
using ludus::foundation::float32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;

// A captured frame of the gym run: the system snapshot plus the voice info for
// every handle the scenario is tracking, at a known RenderFrame.
struct Capture final
{
    uint64 RenderFrame = 0;
    SystemSnapshot System{};
    std::vector<VoiceInfo> Voices;
    std::vector<OwnerEvent> OwnerEvents;
};

// A named, reproducible offline scenario. Build one, drive it with Advance /
// Submit, and read Captures. All data is owner-side and copyable so a scenario
// can be exported to JSON by a cold tool (not part of the gym itself).
class Scenario final
{
public:
    Scenario(std::string name, const SystemConfig& config) noexcept;
    ~Scenario() noexcept;

    Scenario(const Scenario&) = delete;
    Scenario& operator=(const Scenario&) = delete;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return mSystem.IsValid();
    }
    [[nodiscard]] AudioSystem& System() noexcept
    {
        return mSystem;
    }
    [[nodiscard]] const std::string& Name() const noexcept
    {
        return mName;
    }

    // Prepare a clip from encoded bytes; records it for tracking by name.
    [[nodiscard]] Status
    PrepareClip(std::span<const uint8> encoded, const ClipDescriptor& descriptor, ClipHandle& out) noexcept;

    // Track a voice handle so subsequent captures include its VoiceInfo.
    void Track(VoiceHandle voice) noexcept;

    // Advance the offline renderer by `frames` (stereo interleaved scratch) and
    // append a capture of the state afterwards.
    [[nodiscard]] Status Advance(uint32 frames) noexcept;

    [[nodiscard]] const std::vector<Capture>& Captures() const noexcept
    {
        return mCaptures;
    }

    // Filtering helpers over the most recent capture.
    [[nodiscard]] std::vector<VoiceInfo> FilterByGroup(uint32 groupIndex) const noexcept;
    [[nodiscard]] std::vector<VoiceInfo> FilterByBus(uint32 busIndex) const noexcept;
    [[nodiscard]] std::vector<VoiceInfo> FilterByTag(uint32 policyTag) const noexcept;

private:
    std::string mName;
    AudioSystem mSystem;
    std::vector<VoiceHandle> mTracked;
    std::vector<Capture> mCaptures;
    std::vector<float32> mScratch;
};

} // namespace ludus::audio::gym
