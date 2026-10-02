// Offline audio gym scenario tests plus the remaining A1 truth-table rows that
// are naturally expressed through a multi-step offline run (design section 11;
// tasks A1 gate). Owner-side only; no device/worker/Platform/window.

#include <ludus/audio/audio_gym.h>
#include <ludus/audio/audio_system.h>

#include "wav_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace ludus::audio;

namespace
{
SystemConfig OfflineConfig()
{
    SystemConfig cfg{};
    cfg.SystemMode = Mode::Offline;
    cfg.SampleRate = 48000;
    return cfg;
}

std::vector<uint8> Sine(uint32 frames = 4800)
{
    return test::MakeSineWav(48000, 1, frames);
}
} // namespace

TEST_CASE("Gym runs a named scenario and captures tracked voices", "[audio][gym]")
{
    gym::Scenario scenario("single-oneshot", OfflineConfig());
    REQUIRE(scenario.IsValid());

    auto wav = Sine();
    ClipHandle clip{};
    ClipDescriptor d{};
    REQUIRE(IsOk(scenario.PrepareClip(std::span<const uint8>(wav.data(), wav.size()), d, clip)));

    PlayParams p{};
    p.Clip = clip;
    p.PolicyTag = 42;
    VoiceHandle v{};
    REQUIRE(IsOk(scenario.System().PlayClip(p, v)));
    scenario.Track(v);

    REQUIRE(IsOk(scenario.Advance(128)));
    REQUIRE(IsOk(scenario.Advance(128)));
    REQUIRE(scenario.Captures().size() == 2);
    REQUIRE(scenario.Captures().back().RenderFrame == 256);

    // The tracked voice is visible and filterable by its policy tag.
    const auto tagged = scenario.FilterByTag(42);
    REQUIRE(tagged.size() == 1);
    REQUIRE(tagged[0].PolicyTag == 42);
}

TEST_CASE("Gym group-saturation scenario reports admitted/selected counts", "[audio][gym]")
{
    GroupConfig groups[1]{};
    groups[0].MaxAdmitted = 4;
    groups[0].MaxSelected = 2;
    SystemConfig cfg = OfflineConfig();
    cfg.Groups = std::span<const GroupConfig>(groups, 1);

    gym::Scenario scenario("group-saturation", cfg);
    REQUIRE(scenario.IsValid());
    auto wav = Sine();
    ClipHandle clip{};
    ClipDescriptor d{};
    REQUIRE(IsOk(scenario.PrepareClip(std::span<const uint8>(wav.data(), wav.size()), d, clip)));

    for (int i = 0; i < 4; ++i)
    {
        VoiceHandle v{};
        PlayParams p{};
        p.Clip = clip;
        p.Looping = true;                             // keep them alive
        p.Policy = VirtualPolicy::AdvanceWhenVirtual; // unselected -> Virtual, still admitted
        REQUIRE(IsOk(scenario.System().PlayClip(p, v)));
        scenario.Track(v);
    }
    // Fifth exceeds MaxAdmitted.
    VoiceHandle extra{};
    PlayParams pe{};
    pe.Clip = clip;
    REQUIRE(scenario.System().PlayClip(pe, extra) == Status::GroupCapacity);

    REQUIRE(IsOk(scenario.Advance(128)));
    GroupInfo gi{};
    REQUIRE(IsOk(scenario.System().GetGroupInfo(0, gi)));
    // All four remain admitted; only MaxSelected are selected, the rest virtual.
    REQUIRE(gi.Admitted == 4);
    REQUIRE(gi.Selected <= 2);
}

TEST_CASE("A batch straddling the 64-command boundary budget makes progress", "[audio][gym]")
{
    // Publish more total commands than one 128-frame boundary applies (64), then
    // render repeatedly. All published work must eventually be applied without a
    // partial-batch update. Each batch is <= MAX_BATCH_RECORDS; several batches
    // together exceed the per-boundary budget.
    gym::Scenario scenario("budget-straddle", OfflineConfig());
    auto wav = Sine();
    ClipHandle clip{};
    ClipDescriptor d{};
    REQUIRE(IsOk(scenario.PrepareClip(std::span<const uint8>(wav.data(), wav.size()), d, clip)));

    uint32 submitted = 0;
    for (int b = 0; b < 5; ++b) // 5 batches x 20 plays = 100 > 64 budget
    {
        std::vector<Command> cmds(20);
        for (auto& c : cmds)
        {
            c.Kind = CommandKind::Play;
            c.Play.Clip = clip;
        }
        const BatchResult r = scenario.System().TrySubmitBatch(std::span<const Command>(cmds.data(), cmds.size()));
        if (IsOk(r.Result))
        {
            submitted += static_cast<uint32>(r.Voices.size());
        }
    }
    REQUIRE(submitted == 100);

    // Render enough boundaries to drain all published commands (>=2 boundaries).
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(IsOk(scenario.Advance(128)));
    }
    SystemSnapshot snap{};
    scenario.System().GetSystemSnapshot(snap);
    // All 100 plays were consumed (accepted command count reflects progress).
    REQUIRE(snap.AcceptedCommands >= 100);
}

TEST_CASE("A newly accepted voice is immediately queryable as Pending", "[audio][gym]")
{
    // GetVoiceInfo must expose an accepted reservation before the first render
    // snapshot (design section 4).
    gym::Scenario scenario("query-pending", OfflineConfig());
    auto wav = Sine();
    ClipHandle clip{};
    ClipDescriptor d{};
    REQUIRE(IsOk(scenario.PrepareClip(std::span<const uint8>(wav.data(), wav.size()), d, clip)));

    VoiceHandle v{};
    REQUIRE(IsOk(scenario.System().PlayClip({ .Clip = clip }, v)));

    VoiceInfo info{};
    REQUIRE(IsOk(scenario.System().GetVoiceInfo(v, info)));
    REQUIRE(info.State == VoiceState::Pending); // known before any render
}
