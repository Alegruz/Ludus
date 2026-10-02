// Admission / validation / transactional rollback tests via the production
// public path (design section 4; tasks A1 gate rows). No device, worker,
// Platform or window.

#include <ludus/audio/audio_system.h>

#include "wav_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace ludus::audio;

namespace
{
void InitOffline(AudioSystem& sys, uint32 logical = 0, std::span<const GroupConfig> groups = {})
{
    SystemConfig cfg{};
    cfg.SystemMode = Mode::Offline;
    cfg.SampleRate = 48000;
    cfg.LogicalVoiceCapacity = logical;
    cfg.Groups = groups;
    const Status s = sys.Initialize(cfg);
    REQUIRE(IsOk(s));
}

ClipHandle PrepareSine(AudioSystem& sys, uint16 channels = 1, uint32 frames = 480)
{
    auto wav = test::MakeSineWav(48000, channels, frames);
    ClipHandle h{};
    ClipDescriptor d{};
    d.Format = SourceFormat::Wav;
    const Status s = sys.PrepareClip(std::span<const uint8>(wav.data(), wav.size()), d, h);
    REQUIRE(IsOk(s));
    REQUIRE(h.IsValid());
    return h;
}
} // namespace

TEST_CASE("Fabricated / cross-system / stale handles are rejected safely", "[audio][admission]")
{
    AudioSystem sys;
    InitOffline(sys);
    VoiceInfo info{};
    REQUIRE(sys.GetVoiceInfo(VoiceHandle{}, info) == Status::InvalidHandle);
    REQUIRE(sys.GetVoiceInfo(VoiceHandle{999, 5, 7}, info) == Status::InvalidHandle);
    REQUIRE(sys.Stop(VoiceHandle{999, 5, 7}) == Status::InvalidHandle);

    // A clip handle from a different (fabricated) session cannot play.
    PlayParams p{};
    p.Clip = ClipHandle{12345, 0, 1};
    VoiceHandle v{};
    REQUIRE(sys.PlayClip(p, v) == Status::InvalidHandle);
}

TEST_CASE("An invalid last record rejects the whole batch with no leaked pins", "[audio][admission]")
{
    AudioSystem sys;
    InitOffline(sys);
    ClipHandle clip = PrepareSine(sys);

    SystemSnapshot before{};
    sys.GetSystemSnapshot(before);

    Command cmds[2]{};
    cmds[0].Kind = CommandKind::Play;
    cmds[0].Play.Clip = clip;
    cmds[1].Kind = CommandKind::Play;
    cmds[1].Play.Clip = clip;
    cmds[1].Play.Priority = 99; // invalid priority => reject whole batch

    const BatchResult r = sys.TrySubmitBatch(std::span<const Command>(cmds, 2));
    REQUIRE(r.Result == Status::InvalidArgument);
    REQUIRE(r.Voices.empty());

    SystemSnapshot after{};
    sys.GetSystemSnapshot(after);
    REQUIRE(after.PendingVoices == 0); // no earlier play applied
}

TEST_CASE("Oversized batch is rejected", "[audio][admission]")
{
    AudioSystem sys;
    InitOffline(sys);
    ClipHandle clip = PrepareSine(sys);
    std::vector<Command> big(MAX_BATCH_RECORDS + 1);
    for (auto& c : big)
    {
        c.Kind = CommandKind::Play;
        c.Play.Clip = clip;
    }
    const BatchResult r = sys.TrySubmitBatch(std::span<const Command>(big.data(), big.size()));
    REQUIRE(r.Result == Status::InvalidArgument);
}

TEST_CASE("Group MaxAdmitted is transactional and returns GroupCapacity", "[audio][admission]")
{
    GroupConfig groups[1]{};
    groups[0].MaxAdmitted = 2;
    groups[0].MaxSelected = 2;
    AudioSystem sys;
    InitOffline(sys, 0, std::span<const GroupConfig>(groups, 1));
    ClipHandle clip = PrepareSine(sys);

    // Two plays fit.
    VoiceHandle v{};
    PlayParams p{};
    p.Clip = clip;
    REQUIRE(IsOk(sys.PlayClip(p, v)));
    REQUIRE(IsOk(sys.PlayClip(p, v)));

    // A third exceeds MaxAdmitted.
    REQUIRE(sys.PlayClip(p, v) == Status::GroupCapacity);

    GroupInfo gi{};
    REQUIRE(IsOk(sys.GetGroupInfo(0, gi)));
    REQUIRE(gi.Admitted == 2);

    // A batch of two where the second overflows the group must roll back both.
    Command cmds[2]{};
    cmds[0].Kind = CommandKind::Play;
    cmds[0].Play.Clip = clip; // would be the 3rd admitted (already full)
    cmds[1].Kind = CommandKind::Play;
    cmds[1].Play.Clip = clip;
    const BatchResult r = sys.TrySubmitBatch(std::span<const Command>(cmds, 2));
    REQUIRE(r.Result == Status::GroupCapacity);
    REQUIRE(IsOk(sys.GetGroupInfo(0, gi)));
    REQUIRE(gi.Admitted == 2); // unchanged
}

TEST_CASE("Disabled mode never creates a phantom voice", "[audio][admission]")
{
    AudioSystem sys;
    SystemConfig cfg{};
    cfg.SystemMode = Mode::Disabled;
    cfg.SampleRate = 48000;
    REQUIRE(IsOk(sys.Initialize(cfg)));

    PlayParams p{};
    p.Clip = ClipHandle{sys.GetSession(), 0, 1};
    VoiceHandle v{};
    const Status s = sys.PlayClip(p, v);
    REQUIRE((s == Status::Disabled || s == Status::InvalidHandle));
    SystemSnapshot snap{};
    sys.GetSystemSnapshot(snap);
    REQUIRE(snap.PendingVoices == 0);
    REQUIRE(snap.RenderFrame == 0);
}

TEST_CASE("Invalid config and bus topology are rejected before mutation", "[audio][admission]")
{
    // Cyclic/forward parent reference is rejected.
    BusConfig buses[2]{};
    buses[0].Id = 0;
    buses[0].ParentIndex = 1; // parent appears later => invalid (not acyclic precompute)
    buses[1].Id = 1;
    buses[1].ParentIndex = kNoParent;
    AudioSystem sys;
    SystemConfig cfg{};
    cfg.SystemMode = Mode::Offline;
    cfg.SampleRate = 48000;
    cfg.Buses = std::span<const BusConfig>(buses, 2);
    REQUIRE(sys.Initialize(cfg) == Status::InvalidArgument);

    // Zero sample rate rejected.
    AudioSystem sys2;
    SystemConfig cfg2{};
    cfg2.SampleRate = 0;
    REQUIRE(sys2.Initialize(cfg2) == Status::InvalidArgument);
}
