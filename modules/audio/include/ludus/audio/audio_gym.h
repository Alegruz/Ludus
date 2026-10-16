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
#include <ludus/foundation/containers/array.hpp>

#include <span>
#include <string>

namespace ludus::audio::gym
{
using ludus::foundation::float32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;

/// @brief A captured frame of the gym run: the system snapshot plus the voice info for
/// every handle the scenario is tracking, at a known RenderFrame.
struct Capture final
{
    uint64 RenderFrame = 0;
    SystemSnapshot System{};
    /// VoiceInfo for each tracked handle that was live at RenderFrame, in tracking order.
    ludus::foundation::Array<VoiceInfo> Voices;
    /// Owner events observed with this capture. The gym does not populate it yet.
    ludus::foundation::Array<OwnerEvent> OwnerEvents;
};

/// @brief A named, reproducible offline scenario.
///
/// Build one, drive it with Advance / Submit, and read Captures. All data is owner-side and copyable so a scenario
/// can be exported to JSON by a cold tool (not part of the gym itself).
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

    /// Prepare a clip from encoded bytes; records it for tracking by name.
    [[nodiscard]] Status
    PrepareClip(std::span<const uint8> encoded, const ClipDescriptor& descriptor, ClipHandle& out) noexcept;

    /// @brief Track a voice handle so subsequent captures include its VoiceInfo.
    /// @return Ok, or OutOfMemory with the tracked set unchanged.
    [[nodiscard]] Status Track(VoiceHandle voice) noexcept;

    /// @brief Advance the offline renderer by `frames` (stereo interleaved scratch) and
    /// append a capture of the state afterwards.
    ///
    /// Scratch and capture storage are allocated before rendering. OutOfMemory therefore leaves the system,
    /// the captures and the tracked set unchanged; otherwise the capture is appended and the render status is
    /// returned.
    [[nodiscard]] Status Advance(uint32 frames) noexcept;

    /// @brief Captures in Advance order. The reference is invalidated by the next successful Advance.
    [[nodiscard]] const ludus::foundation::Array<Capture>& Captures() const noexcept
    {
        return mCaptures;
    }

    /// @brief Replace outVoices with the most recent capture's voices in group `groupIndex`.
    ///
    /// No capture yields an empty result.
    /// @return Ok, or OutOfMemory with outVoices unchanged.
    [[nodiscard]] Status FilterByGroup(uint32 groupIndex,
                                       ludus::foundation::Array<VoiceInfo>& outVoices) const noexcept;
    /// @brief Replace outVoices with the most recent capture's voices routed to bus `busIndex`.
    /// @return Ok, or OutOfMemory with outVoices unchanged.
    [[nodiscard]] Status FilterByBus(uint32 busIndex, ludus::foundation::Array<VoiceInfo>& outVoices) const noexcept;
    /// @brief Replace outVoices with the most recent capture's voices carrying `policyTag`.
    /// @return Ok, or OutOfMemory with outVoices unchanged.
    [[nodiscard]] Status FilterByTag(uint32 policyTag, ludus::foundation::Array<VoiceInfo>& outVoices) const noexcept;

private:
    std::string mName;
    AudioSystem mSystem;
    ludus::foundation::Array<VoiceHandle> mTracked;
    ludus::foundation::Array<Capture> mCaptures;
    ludus::foundation::Array<float32> mScratch;
};

} // namespace ludus::audio::gym
