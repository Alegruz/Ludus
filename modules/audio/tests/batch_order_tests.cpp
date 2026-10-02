// Regression tests for batch ordering / no-split-batch / batch-local references
// (design section 4; AU05). These close gaps found in the audit: mixed-order
// batches must preserve Play/Stop order; a batch must never be split across the
// 64-record boundary budget; batch-local Update/Stop references must resolve to
// the reserved slot. No device/worker/Platform/window.

#include <ludus/audio/audio_system.h>

#include "wav_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace ludus::audio;

namespace
{
void InitOffline(AudioSystem& sys)
{
    SystemConfig cfg{};
    cfg.SystemMode = Mode::Offline;
    cfg.SampleRate = 48000;
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
} // namespace

TEST_CASE("A batch-local Stop after a Play cancels that Play in one batch", "[audio][batch]")
{
    // [Play(clip), Stop(local 0)] in one batch: the Play is reserved, then the
    // Stop targets it by batch-local index. After a render boundary the voice is
    // cancelled with no sample path leaked.
    AudioSystem sys;
    InitOffline(sys);
    ClipHandle clip = Prepare(sys);

    Command cmds[2]{};
    cmds[0].Kind = CommandKind::Play;
    cmds[0].Play.Clip = clip;
    cmds[0].Play.Looping = true;
    cmds[1].Kind = CommandKind::StopVoice;
    cmds[1].StopBatchLocalPlayIndex = 0; // reference the Play reserved above

    const BatchResult r = sys.TrySubmitBatch(std::span<const Command>(cmds, 2));
    REQUIRE(IsOk(r.Result));
    REQUIRE(r.Voices.size() == 1);
    const VoiceHandle v = r.Voices[0];

    Render(sys, 256);
    VoiceInfo info{};
    const Status s = sys.GetVoiceInfo(v, info);
    // The stop (ordered after the play) terminates it; the play is not left live.
    if (IsOk(s))
    {
        REQUIRE((info.State == VoiceState::Terminal || info.State == VoiceState::Stopping));
    }
    else
    {
        REQUIRE(s == Status::InvalidHandle);
    }
}

TEST_CASE("A batch-local Update after a Play adjusts that Play's gain", "[audio][batch]")
{
    AudioSystem sys;
    InitOffline(sys);
    ClipHandle clip = Prepare(sys);

    Command cmds[2]{};
    cmds[0].Kind = CommandKind::Play;
    cmds[0].Play.Clip = clip;
    cmds[0].Play.Looping = true;
    cmds[0].Play.Gain = 1.0F;
    cmds[1].Kind = CommandKind::UpdateVoice;
    cmds[1].Update.BatchLocalPlayIndex = 0;
    cmds[1].Update.SetGain = true;
    cmds[1].Update.Gain = 0.25F;

    const BatchResult r = sys.TrySubmitBatch(std::span<const Command>(cmds, 2));
    REQUIRE(IsOk(r.Result));
    const VoiceHandle v = r.Voices[0];

    Render(sys, 512);
    VoiceInfo info{};
    REQUIRE(IsOk(sys.GetVoiceInfo(v, info)));
    // The gain update applied (effective gain reflects 0.25 * equal-power pan).
    REQUIRE(info.EffectiveGain < 0.3F);
    REQUIRE(info.EffectiveGain > 0.0F);
}

TEST_CASE("An invalid batch-local reference rejects the whole batch", "[audio][batch]")
{
    AudioSystem sys;
    InitOffline(sys);
    ClipHandle clip = Prepare(sys);

    // Update references local index 1, but command 1 is the Update itself (not a
    // Play), and index >= its own position => invalid.
    Command cmds[2]{};
    cmds[0].Kind = CommandKind::Play;
    cmds[0].Play.Clip = clip;
    cmds[1].Kind = CommandKind::UpdateVoice;
    cmds[1].Update.BatchLocalPlayIndex = 1; // self/forward reference: invalid

    const BatchResult r = sys.TrySubmitBatch(std::span<const Command>(cmds, 2));
    REQUIRE(r.Result == Status::InvalidArgument);
    REQUIRE(r.Voices.empty());
    SystemSnapshot snap{};
    sys.GetSystemSnapshot(snap);
    REQUIRE(snap.PendingVoices == 0); // the earlier Play was not applied
}

TEST_CASE("A batch is never split across the 64-command boundary budget", "[audio][batch]")
{
    // Submit batches summing to >64 records, then render exactly ONE boundary.
    // The number of commands applied in that single boundary must be a sum of
    // WHOLE batches (never a partial batch). With 3 batches of 25 (75 records)
    // and a 64-record budget, exactly 2 batches (50) apply at boundary 1 and the
    // third (25) defers -- 50, not 64.
    AudioSystem sys;
    InitOffline(sys);
    ClipHandle clip = Prepare(sys);

    const uint32 kBatch = 25;
    for (int b = 0; b < 3; ++b)
    {
        std::vector<Command> cmds(kBatch);
        for (auto& c : cmds)
        {
            c.Kind = CommandKind::Play;
            c.Play.Clip = clip;
            c.Play.Looping = true;
        }
        const BatchResult r = sys.TrySubmitBatch(std::span<const Command>(cmds.data(), cmds.size()));
        REQUIRE(IsOk(r.Result));
    }

    // Render exactly one 128-frame control boundary.
    Render(sys, 128);
    SystemSnapshot snap{};
    sys.GetSystemSnapshot(snap);
    // Whole batches only: 50 applied (2 x 25), not 64 (which would split batch 3).
    REQUIRE(snap.AcceptedCommands == 50);

    // A second boundary drains the deferred third batch.
    Render(sys, 128);
    sys.GetSystemSnapshot(snap);
    REQUIRE(snap.AcceptedCommands == 75);
}

TEST_CASE("Virtual voices advance on the same timeline as mixed (no drift)", "[audio][batch]")
{
    // A finite non-integer-rate voice forced virtual must expire at the same
    // output time as if it had been mixed. Rate 1.3 over a 48000-frame clip
    // expires after ceil(48000/1.3) ~= 36924 output frames. With the fractional
    // cursor carried, the virtual expiry lands close to that; a truncating
    // advance would expire noticeably later.
    GroupConfig g[1]{};
    g[0].MaxAdmitted = 8;
    g[0].MaxSelected = 1; // one selected; others virtual (advance policy)
    AudioSystem sys;
    SystemConfig cfg{};
    cfg.SystemMode = Mode::Offline;
    cfg.SampleRate = 48000;
    cfg.Groups = std::span<const GroupConfig>(g, 1);
    REQUIRE(IsOk(sys.Initialize(cfg)));
    ClipHandle clip = Prepare(sys, 48000);

    // Two advance-policy voices; the louder wins selection, the quieter goes
    // virtual and advances.
    PlayParams loud{};
    loud.Clip = clip;
    loud.Gain = 1.0F;
    loud.Rate = 1.3F;
    loud.Policy = VirtualPolicy::AdvanceWhenVirtual;
    PlayParams quiet = loud;
    quiet.Gain = 0.5F;
    VoiceHandle vl{};
    VoiceHandle vq{};
    REQUIRE(IsOk(sys.PlayClip(loud, vl)));
    REQUIRE(IsOk(sys.PlayClip(quiet, vq)));

    // Render well past expected expiry (~28.4k frames for rate 1.3 over 48000)
    // and verify the virtual voice expired (terminal/reclaimed), not stuck.
    Render(sys, 40000);
    VoiceInfo info{};
    const Status s = sys.GetVoiceInfo(vq, info);
    if (IsOk(s))
    {
        REQUIRE(info.State == VoiceState::Terminal);
    }
    else
    {
        REQUIRE(s == Status::InvalidHandle);
    }
}
