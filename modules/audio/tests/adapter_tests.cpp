// Application event-adapter tests (design section 9; tasks A3 gate). The adapter
// is an application example over the public API; these verify owner-side policy
// (cooldown / AlreadyActive) before admission, table-full, failure rollback,
// owned-loop teardown and the shared dialogue duck. No device/worker/window.

#include <ludus/audio/audio_app_adapter.h>
#include <ludus/audio/audio_system.h>

#include "wav_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace ludus::audio;
using ludus::audio::app::EventAdapter;
using ludus::audio::app::SuppressReason;
using ludus::audio::app::TriggerRequest;

namespace
{
void InitOffline(AudioSystem& sys, std::span<const BusConfig> buses = {})
{
    SystemConfig cfg{};
    cfg.SystemMode = Mode::Offline;
    cfg.SampleRate = 48000;
    cfg.Buses = buses;
    REQUIRE(IsOk(sys.Initialize(cfg)));
}

ClipHandle PrepareLoopable(AudioSystem& sys, uint32 frames = 480)
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
} // namespace

TEST_CASE("Cooldown suppresses a rapid repeat before admission", "[audio][adapter]")
{
    AudioSystem sys;
    InitOffline(sys);
    ClipHandle clip = PrepareLoopable(sys);
    EventAdapter adapter(sys);

    TriggerRequest req{};
    req.EventId = 7;
    req.OwnerTag = 1;
    req.CooldownTicks = 10;
    req.SuppressWhileActive = false;
    req.Play.Clip = clip;

    // First trigger admits.
    auto r1 = adapter.Trigger(req, 100);
    REQUIRE(r1.Admitted);
    // Immediate repeat within cooldown is suppressed (no engine admission).
    auto r2 = adapter.Trigger(req, 105);
    REQUIRE_FALSE(r2.Admitted);
    REQUIRE(r2.Suppressed == SuppressReason::Cooldown);
    // After cooldown elapses it admits again.
    auto r3 = adapter.Trigger(req, 111);
    REQUIRE(r3.Admitted);
}

TEST_CASE("AlreadyActive suppresses while an accepted instance is still live", "[audio][adapter]")
{
    AudioSystem sys;
    InitOffline(sys);
    ClipHandle clip = PrepareLoopable(sys, 48000); // long clip, stays active
    EventAdapter adapter(sys);

    TriggerRequest req{};
    req.EventId = 3;
    req.OwnerTag = 2;
    req.SuppressWhileActive = true;
    req.Play.Clip = clip;
    req.Play.Looping = true;

    auto r1 = adapter.Trigger(req, 0);
    REQUIRE(r1.Admitted);
    // The accepted voice is Pending (not yet rendered) but counts as active.
    auto r2 = adapter.Trigger(req, 1);
    REQUIRE_FALSE(r2.Admitted);
    REQUIRE(r2.Suppressed == SuppressReason::AlreadyActive);
}

TEST_CASE("Failed engine admission consumes no cooldown and leaves no state", "[audio][adapter]")
{
    GroupConfig g[1]{};
    g[0].MaxAdmitted = 1;
    g[0].MaxSelected = 1;
    AudioSystem sys;
    SystemConfig cfg{};
    cfg.SystemMode = Mode::Offline;
    cfg.SampleRate = 48000;
    cfg.Groups = std::span<const GroupConfig>(g, 1);
    REQUIRE(IsOk(sys.Initialize(cfg)));
    ClipHandle clip = PrepareLoopable(sys, 48000);
    EventAdapter adapter(sys);

    // Occupy the single group admission with a direct play.
    VoiceHandle occupy{};
    REQUIRE(IsOk(sys.PlayClip({ .Clip = clip, .Looping = true }, occupy)));

    TriggerRequest req{};
    req.EventId = 9;
    req.OwnerTag = 5;
    req.CooldownTicks = 100;
    req.Play.Clip = clip;

    auto r = adapter.Trigger(req, 50);
    REQUIRE_FALSE(r.Admitted);
    REQUIRE(r.EngineStatus == Status::GroupCapacity);
    REQUIRE(r.Suppressed == SuppressReason::None); // not a policy suppression

    // Because admission failed, cooldown was NOT consumed: once the group frees
    // up, a retry admits immediately (no cooldown wait).
    REQUIRE(IsOk(sys.Stop(occupy)));
    Render(sys, 1024);
    adapter.Update(60);
    auto r2 = adapter.Trigger(req, 60);
    REQUIRE(r2.Admitted);
}

TEST_CASE("A full policy table reports TableFull for a new key", "[audio][adapter]")
{
    AudioSystem sys;
    InitOffline(sys);
    ClipHandle clip = PrepareLoopable(sys, 48000);
    EventAdapter adapter(sys);

    // Fill the policy table with distinct owner tags, each with a cooldown so the
    // entries persist.
    for (uint32 i = 0; i < app::POLICY_TABLE_CAPACITY; ++i)
    {
        TriggerRequest req{};
        req.EventId = 1;
        req.OwnerTag = i + 1;
        req.CooldownTicks = 1000;
        req.Play.Clip = clip;
        auto r = adapter.Trigger(req, 10);
        REQUIRE(r.Admitted);
    }
    // A new key has no room.
    TriggerRequest extra{};
    extra.EventId = 1;
    extra.OwnerTag = 99999;
    extra.CooldownTicks = 1000;
    extra.Play.Clip = clip;
    auto r = adapter.Trigger(extra, 10);
    REQUIRE_FALSE(r.Admitted);
    REQUIRE(r.Suppressed == SuppressReason::TableFull);
}

TEST_CASE("Owned loops stop on entity teardown", "[audio][adapter]")
{
    AudioSystem sys;
    InitOffline(sys);
    ClipHandle clip = PrepareLoopable(sys, 48000);
    EventAdapter adapter(sys);

    TriggerRequest req{};
    req.EventId = 2;
    req.OwnerTag = 42;
    req.Play.Clip = clip;
    req.Play.Looping = true;
    req.IsOwnedLoop = true;
    auto r = adapter.Trigger(req, 0);
    REQUIRE(r.Admitted);
    Render(sys, 256);

    // Entity removed: owned loops for tag 42 must be stopped. Render enough to
    // let the stop fade complete and the voice terminate.
    adapter.StopOwnedLoops(42);
    Render(sys, 2048);
    Render(sys, 2048);
    VoiceInfo info{};
    const Status s = sys.GetVoiceInfo(r.Voice, info);
    if (IsOk(s))
    {
        REQUIRE(info.State == VoiceState::Terminal);
    }
    else
    {
        REQUIRE(s == Status::InvalidHandle); // reclaimed
    }
}

TEST_CASE("Shared dialogue duck releases only after the last member terminates", "[audio][adapter]")
{
    // Master(0) <- Dialogue(1) <- others; duck applied to a music bus (2).
    BusConfig buses[3]{};
    buses[0] = BusConfig{ .Id = 0, .ParentIndex = kNoParent };
    buses[1] = BusConfig{ .Id = 1, .ParentIndex = 0 };
    buses[2] = BusConfig{ .Id = 2, .ParentIndex = 0 };
    AudioSystem sys;
    InitOffline(sys, std::span<const BusConfig>(buses, 3));
    ClipHandle clip = PrepareLoopable(sys, 2400); // 50 ms lines
    EventAdapter adapter(sys);
    adapter.ConfigureDialogueDuck(2, -12.0F);

    TriggerRequest a{};
    a.EventId = 100;
    a.OwnerTag = 1;
    a.Play.Clip = clip;
    a.Play.BusIndex = 1;
    TriggerRequest b = a;
    b.OwnerTag = 2;

    auto ra = adapter.TriggerDialogue(a, 0);
    REQUIRE(ra.Admitted);
    REQUIRE(adapter.DialogueDuckActive());
    auto rb = adapter.TriggerDialogue(b, 1);
    REQUIRE(rb.Admitted);
    REQUIRE(adapter.DialogueMemberCount() == 2);

    // Render past the clip length so both lines finish, servicing + updating.
    for (int i = 0; i < 4; ++i)
    {
        Render(sys, 2048);
        adapter.Update(10 + i);
    }
    // After the last member durably terminates, the duck is released.
    REQUIRE_FALSE(adapter.DialogueDuckActive());
    REQUIRE(adapter.DialogueMemberCount() == 0);
}

TEST_CASE("A failed dialogue admission creates no member and no duck", "[audio][adapter]")
{
    BusConfig buses[3]{};
    buses[0] = BusConfig{ .Id = 0, .ParentIndex = kNoParent };
    buses[1] = BusConfig{ .Id = 1, .ParentIndex = 0 };
    buses[2] = BusConfig{ .Id = 2, .ParentIndex = 0 };
    GroupConfig g[1]{};
    g[0].MaxAdmitted = 1;
    g[0].MaxSelected = 1;
    AudioSystem sys;
    SystemConfig cfg{};
    cfg.SystemMode = Mode::Offline;
    cfg.SampleRate = 48000;
    cfg.Buses = std::span<const BusConfig>(buses, 3);
    cfg.Groups = std::span<const GroupConfig>(g, 1);
    REQUIRE(IsOk(sys.Initialize(cfg)));
    ClipHandle clip = PrepareLoopable(sys, 48000);
    EventAdapter adapter(sys);
    adapter.ConfigureDialogueDuck(2, -12.0F);

    // Occupy the only admission slot.
    VoiceHandle occupy{};
    REQUIRE(IsOk(sys.PlayClip({ .Clip = clip, .Looping = true }, occupy)));

    TriggerRequest a{};
    a.EventId = 100;
    a.OwnerTag = 1;
    a.Play.Clip = clip;
    a.Play.BusIndex = 1;
    auto r = adapter.TriggerDialogue(a, 0);
    REQUIRE_FALSE(r.Admitted);
    REQUIRE_FALSE(adapter.DialogueDuckActive()); // no duck on failure
    REQUIRE(adapter.DialogueMemberCount() == 0);
}
