// Explainable-diagnostics tests: independent silence-cause flags recomputed per
// boundary, and muted-finite expiry with no unmute backlog (design sections 7,
// 11; Game Audio Programming vol 2 ch.9). Production paths only.

#include <ludus/audio/audio_system.h>

#include "wav_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace ludus::audio;

namespace
{
// Master(0) <- SFX(1), so we can user-mute SFX.
struct TwoBus final
{
    BusConfig buses[2];
    TwoBus()
    {
        buses[0] = BusConfig{ .Id = 0, .ParentIndex = kNoParent };
        buses[1] = BusConfig{ .Id = 1, .ParentIndex = 0 };
    }
};

void InitTwoBus(AudioSystem& sys, const TwoBus& t)
{
    SystemConfig cfg{};
    cfg.SystemMode = Mode::Offline;
    cfg.SampleRate = 48000;
    cfg.Buses = std::span<const BusConfig>(t.buses, 2);
    REQUIRE(IsOk(sys.Initialize(cfg)));
}

ClipHandle Prepare(AudioSystem& sys, uint32 frames = 48000)
{
    auto wav = test::MakeSineWav(48000, 1, frames);
    ClipHandle h{};
    ClipDescriptor d{};
    REQUIRE(IsOk(sys.PrepareClip(std::span<const uint8>(wav.data(), wav.size()), d, h)));
    return h;
}

void Render(AudioSystem& sys, uint32 frames)
{
    std::vector<float32> out(static_cast<std::size_t>(frames) * 2);
    REQUIRE(IsOk(sys.RenderOffline(std::span<float32>(out.data(), out.size()),
                                   ChannelLayout::Stereo,
                                   BufferLayout::Interleaved,
                                   frames)));
    sys.Service();
}

bool Has(SilenceCause set, SilenceCause flag)
{
    return HasCause(set, flag);
}
} // namespace

TEST_CASE("Silence cause NotStarted is reported before a scheduled start", "[audio][diag]")
{
    AudioSystem sys;
    TwoBus t;
    InitTwoBus(sys, t);
    ClipHandle clip = Prepare(sys);
    PlayParams p{};
    p.Clip = clip;
    p.BusIndex = 1;
    p.StartFrame = 48000; // far ahead -> still NotStarted after a short render
    VoiceHandle v{};
    REQUIRE(IsOk(sys.PlayClip(p, v)));

    Render(sys, 256);
    VoiceInfo info{};
    REQUIRE(IsOk(sys.GetVoiceInfo(v, info)));
    REQUIRE(Has(info.Silence, SilenceCause::NotStarted));
}

TEST_CASE("Silence cause UserMuted is reported when the bus chain is muted", "[audio][diag]")
{
    AudioSystem sys;
    TwoBus t;
    InitTwoBus(sys, t);
    ClipHandle clip = Prepare(sys);

    // User-mute SFX bus.
    Command mute{};
    mute.Kind = CommandKind::SetBusUserGain;
    mute.BusIndex = 1;
    mute.BusGain = 0.0F;
    REQUIRE(IsOk(sys.TrySubmitBatch(std::span<const Command>(&mute, 1)).Result));

    PlayParams p{};
    p.Clip = clip;
    p.BusIndex = 1;
    p.Looping = true;
    p.Policy = VirtualPolicy::AdvanceWhenVirtual; // stays alive while muted
    VoiceHandle v{};
    REQUIRE(IsOk(sys.PlayClip(p, v)));

    Render(sys, 512);
    VoiceInfo info{};
    REQUIRE(IsOk(sys.GetVoiceInfo(v, info)));
    REQUIRE(Has(info.Silence, SilenceCause::UserMuted));
}

TEST_CASE("Silence cause GroupQuota is reported for a quota loser", "[audio][diag]")
{
    GroupConfig g[1]{};
    g[0].MaxAdmitted = 4;
    g[0].MaxSelected = 1; // only one may be selected
    AudioSystem sys;
    SystemConfig cfg{};
    cfg.SystemMode = Mode::Offline;
    cfg.SampleRate = 48000;
    cfg.Groups = std::span<const GroupConfig>(g, 1);
    REQUIRE(IsOk(sys.Initialize(cfg)));
    ClipHandle clip = Prepare(sys);

    VoiceHandle v1{};
    VoiceHandle v2{};
    PlayParams p{};
    p.Clip = clip;
    p.Looping = true;
    p.Policy = VirtualPolicy::AdvanceWhenVirtual;
    REQUIRE(IsOk(sys.PlayClip(p, v1)));
    REQUIRE(IsOk(sys.PlayClip(p, v2)));

    Render(sys, 512);
    // Exactly one is selected; the other reports GroupQuota and is virtual.
    VoiceInfo i1{};
    VoiceInfo i2{};
    REQUIRE(IsOk(sys.GetVoiceInfo(v1, i1)));
    REQUIRE(IsOk(sys.GetVoiceInfo(v2, i2)));
    const bool oneQuota = Has(i1.Silence, SilenceCause::GroupQuota) || Has(i2.Silence, SilenceCause::GroupQuota);
    REQUIRE(oneQuota);
}

TEST_CASE("Silence flags are recomputed per boundary (no stale leak)", "[audio][diag]")
{
    AudioSystem sys;
    TwoBus t;
    InitTwoBus(sys, t);
    ClipHandle clip = Prepare(sys);

    // Start muted so UserMuted is set.
    Command mute{};
    mute.Kind = CommandKind::SetBusUserGain;
    mute.BusIndex = 1;
    mute.BusGain = 0.0F;
    REQUIRE(IsOk(sys.TrySubmitBatch(std::span<const Command>(&mute, 1)).Result));

    PlayParams p{};
    p.Clip = clip;
    p.BusIndex = 1;
    p.Looping = true;
    p.Policy = VirtualPolicy::AdvanceWhenVirtual; // survives muting (stays virtual)
    VoiceHandle v{};
    REQUIRE(IsOk(sys.PlayClip(p, v)));
    Render(sys, 256);
    VoiceInfo info{};
    REQUIRE(IsOk(sys.GetVoiceInfo(v, info)));
    REQUIRE(Has(info.Silence, SilenceCause::UserMuted));

    // Unmute: the UserMuted flag must clear on the next boundary (not leak).
    Command unmute{};
    unmute.Kind = CommandKind::SetBusUserGain;
    unmute.BusIndex = 1;
    unmute.BusGain = 1.0F;
    REQUIRE(IsOk(sys.TrySubmitBatch(std::span<const Command>(&unmute, 1)).Result));
    Render(sys, 256);
    REQUIRE(IsOk(sys.GetVoiceInfo(v, info)));
    REQUIRE_FALSE(Has(info.Silence, SilenceCause::UserMuted));
}

TEST_CASE("A muted finite voice expires and unmute creates no backlog", "[audio][diag]")
{
    // An advance-policy finite voice on a user-muted bus must still advance to
    // EOF while muted; a later unmute must not resurrect the finished transient
    // (vol 2 ch.9).
    AudioSystem sys;
    TwoBus t;
    InitTwoBus(sys, t);
    ClipHandle clip = Prepare(sys, 4800); // 100 ms finite one-shot

    Command mute{};
    mute.Kind = CommandKind::SetBusUserGain;
    mute.BusIndex = 1;
    mute.BusGain = 0.0F;
    REQUIRE(IsOk(sys.TrySubmitBatch(std::span<const Command>(&mute, 1)).Result));

    PlayParams p{};
    p.Clip = clip;
    p.BusIndex = 1;
    p.Policy = VirtualPolicy::AdvanceWhenVirtual;
    VoiceHandle v{};
    REQUIRE(IsOk(sys.PlayClip(p, v)));

    // Render well past the clip length while muted: the voice advances to EOF.
    Render(sys, 8192);
    VoiceInfo info{};
    const Status s = sys.GetVoiceInfo(v, info);
    if (IsOk(s))
    {
        REQUIRE(info.State == VoiceState::Terminal);
    }
    else
    {
        REQUIRE(s == Status::InvalidHandle); // reclaimed
    }

    // Unmute and render: no resurrected transient (no new mixed voice appears).
    Command unmute{};
    unmute.Kind = CommandKind::SetBusUserGain;
    unmute.BusIndex = 1;
    unmute.BusGain = 1.0F;
    REQUIRE(IsOk(sys.TrySubmitBatch(std::span<const Command>(&unmute, 1)).Result));
    Render(sys, 2048);
    SystemSnapshot snap{};
    sys.GetSystemSnapshot(snap);
    REQUIRE(snap.MixedVoices == 0);
    REQUIRE(snap.VirtualVoices == 0);
}
