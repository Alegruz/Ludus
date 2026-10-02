// Resident DSP + spatial correctness tests via the production offline render
// path (design sections 5-7; tasks A2 gate). No device/worker/Platform/window.

#include <ludus/audio/audio_system.h>

#include "wav_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using namespace ludus::audio;

namespace
{
void InitOffline(AudioSystem& sys, uint32 rate = 48000)
{
    SystemConfig cfg{};
    cfg.SystemMode = Mode::Offline;
    cfg.SampleRate = rate;
    REQUIRE(IsOk(sys.Initialize(cfg)));
}

ClipHandle Prepare(AudioSystem& sys, const std::vector<uint8>& wav)
{
    ClipHandle h{};
    ClipDescriptor d{};
    REQUIRE(IsOk(sys.PrepareClip(std::span<const uint8>(wav.data(), wav.size()), d, h)));
    return h;
}

// Peak absolute amplitude of an interleaved stereo buffer channel.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): test helper.
float32 ChannelPeak(const std::vector<float32>& out, uint32 frames, uint32 channel)
{
    float32 peak = 0.0F;
    for (uint32 f = 0; f < frames; ++f)
    {
        const float32 a = std::fabs(out[static_cast<std::size_t>(f) * 2 + channel]);
        if (a > peak)
        {
            peak = a;
        }
    }
    return peak;
}
} // namespace

TEST_CASE("A played clip produces audible nonzero output", "[audio][dsp]")
{
    AudioSystem sys;
    InitOffline(sys);
    auto wav = test::MakeSineWav(48000, 1, 48000, 440.0, 0.5);
    ClipHandle clip = Prepare(sys, wav);

    PlayParams p{};
    p.Clip = clip;
    p.ExplicitPan = 0.0F; // centered
    VoiceHandle v{};
    REQUIRE(IsOk(sys.PlayClip(p, v)));

    constexpr uint32 frames = 2048;
    std::vector<float32> out(static_cast<std::size_t>(frames) * 2U);
    REQUIRE(IsOk(sys.RenderOffline(std::span<float32>(out.data(), out.size()),
                                   ChannelLayout::Stereo,
                                   BufferLayout::Interleaved,
                                   frames)));
    // Both channels carry signal (centered equal-power pan ~0.707 each).
    REQUIRE(ChannelPeak(out, frames, 0) > 0.1F);
    REQUIRE(ChannelPeak(out, frames, 1) > 0.1F);
}

TEST_CASE("Hard-left and hard-right pan route to the correct channel", "[audio][dsp]")
{
    AudioSystem sys;
    InitOffline(sys);
    auto wav = test::MakeSineWav(48000, 1, 48000, 440.0, 0.5);
    ClipHandle clip = Prepare(sys, wav);

    SECTION("hard left")
    {
        PlayParams p{};
        p.Clip = clip;
        p.ExplicitPan = -1.0F;
        VoiceHandle v{};
        REQUIRE(IsOk(sys.PlayClip(p, v)));
        constexpr uint32 frames = 2048;
        std::vector<float32> out(static_cast<std::size_t>(frames) * 2U);
        REQUIRE(IsOk(sys.RenderOffline(std::span<float32>(out.data(), out.size()),
                                       ChannelLayout::Stereo,
                                       BufferLayout::Interleaved,
                                       frames)));
        REQUIRE(ChannelPeak(out, frames, 0) > 0.3F);  // left loud
        REQUIRE(ChannelPeak(out, frames, 1) < 0.01F); // right silent
    }
    SECTION("hard right")
    {
        PlayParams p{};
        p.Clip = clip;
        p.ExplicitPan = 1.0F;
        VoiceHandle v{};
        REQUIRE(IsOk(sys.PlayClip(p, v)));
        constexpr uint32 frames = 2048;
        std::vector<float32> out(static_cast<std::size_t>(frames) * 2U);
        REQUIRE(IsOk(sys.RenderOffline(std::span<float32>(out.data(), out.size()),
                                       ChannelLayout::Stereo,
                                       BufferLayout::Interleaved,
                                       frames)));
        REQUIRE(ChannelPeak(out, frames, 0) < 0.01F);
        REQUIRE(ChannelPeak(out, frames, 1) > 0.3F);
    }
}

TEST_CASE("Planar and interleaved adapters carry identical L/R content", "[audio][dsp]")
{
    AudioSystem a;
    AudioSystem b;
    InitOffline(a);
    InitOffline(b);
    // Distinct constant L/R so channel swaps are detectable.
    auto wav = test::MakeLrWav(48000, 48000, 16000, -16000);
    ClipHandle ca = Prepare(a, wav);
    ClipHandle cb = Prepare(b, wav);
    VoiceHandle va{};
    VoiceHandle vb{};
    PlayParams p{};
    p.Clip = ca;
    REQUIRE(IsOk(a.PlayClip(p, va)));
    p.Clip = cb;
    REQUIRE(IsOk(b.PlayClip(p, vb)));

    constexpr uint32 frames = 1024;
    std::vector<float32> inter(static_cast<std::size_t>(frames) * 2);
    std::vector<float32> planar(static_cast<std::size_t>(frames) * 2);
    REQUIRE(IsOk(a.RenderOffline(std::span<float32>(inter.data(), inter.size()),
                                 ChannelLayout::Stereo,
                                 BufferLayout::Interleaved,
                                 frames)));
    REQUIRE(IsOk(b.RenderOffline(std::span<float32>(planar.data(), planar.size()),
                                 ChannelLayout::Stereo,
                                 BufferLayout::Planar,
                                 frames)));
    // Interleaved L == planar first-half; interleaved R == planar second-half.
    for (uint32 f = 0; f < frames; ++f)
    {
        REQUIRE(inter[f * 2 + 0] == planar[f]);
        REQUIRE(inter[f * 2 + 1] == planar[frames + f]);
    }
    // And left and right are distinct (stereo bed preserved, not collapsed).
    REQUIRE(ChannelPeak(inter, frames, 0) > 0.1F);
    REQUIRE(ChannelPeak(inter, frames, 1) > 0.1F);
}

TEST_CASE("Playback rate 2.0 shortens duration; 0.5 lengthens it", "[audio][dsp]")
{
    // A finite clip at Rate 2 reaches EOF in about half the output frames; at
    // Rate 0.5 it takes about twice as long. We measure how many output frames
    // carry signal before the voice terminates.
    auto measureActiveFrames = [](float32 rate) -> uint32 {
        AudioSystem sys;
        InitOffline(sys);
        auto wav = test::MakeSineWav(48000, 1, 4800, 440.0, 0.5); // 100 ms
        ClipHandle clip = Prepare(sys, wav);
        PlayParams p{};
        p.Clip = clip;
        p.Rate = rate;
        VoiceHandle v{};
        REQUIRE(IsOk(sys.PlayClip(p, v)));

        constexpr uint32 total = 16384;
        std::vector<float32> out(static_cast<std::size_t>(total) * 2);
        REQUIRE(IsOk(sys.RenderOffline(std::span<float32>(out.data(), out.size()),
                                       ChannelLayout::Stereo,
                                       BufferLayout::Interleaved,
                                       total)));
        uint32 lastActive = 0;
        for (uint32 f = 0; f < total; ++f)
        {
            if (std::fabs(out[static_cast<std::size_t>(f) * 2]) > 1e-4F ||
                std::fabs(out[static_cast<std::size_t>(f) * 2 + 1]) > 1e-4F)
            {
                lastActive = f;
            }
        }
        return lastActive;
    };

    const uint32 fast = measureActiveFrames(2.0F);
    const uint32 normal = measureActiveFrames(1.0F);
    const uint32 slow = measureActiveFrames(0.5F);
    // Rate 2 ends sooner than Rate 1, which ends sooner than Rate 0.5.
    REQUIRE(fast < normal);
    REQUIRE(normal < slow);
}

TEST_CASE("A looping clip keeps producing signal past its length", "[audio][dsp]")
{
    AudioSystem sys;
    InitOffline(sys);
    auto wav = test::MakeSineWav(48000, 1, 480, 440.0, 0.5); // 10 ms clip
    ClipHandle clip = Prepare(sys, wav);
    PlayParams p{};
    p.Clip = clip;
    p.Looping = true;
    VoiceHandle v{};
    REQUIRE(IsOk(sys.PlayClip(p, v)));

    constexpr uint32 frames = 4800; // 100 ms >> 10 ms clip
    std::vector<float32> out(static_cast<std::size_t>(frames) * 2U);
    REQUIRE(IsOk(sys.RenderOffline(std::span<float32>(out.data(), out.size()),
                                   ChannelLayout::Stereo,
                                   BufferLayout::Interleaved,
                                   frames)));
    // Signal present near the end (would be silent if it stopped at 10 ms).
    float32 tailPeak = 0.0F;
    for (uint32 f = frames - 480; f < frames; ++f)
    {
        tailPeak = std::max(tailPeak, std::fabs(out[static_cast<std::size_t>(f) * 2]));
    }
    REQUIRE(tailPeak > 0.1F);
}

TEST_CASE("44100 <-> 48000 conversion in both directions produces finite audio", "[audio][dsp]")
{
    SECTION("44100 source into 48000 session")
    {
        AudioSystem sys;
        InitOffline(sys, 48000);
        auto wav = test::MakeSineWav(44100, 1, 44100, 440.0, 0.5);
        ClipHandle clip = Prepare(sys, wav);
        VoiceHandle v{};
        REQUIRE(IsOk(sys.PlayClip({ .Clip = clip }, v)));
        constexpr uint32 frames = 4096;
        std::vector<float32> out(static_cast<std::size_t>(frames) * 2U);
        REQUIRE(IsOk(sys.RenderOffline(std::span<float32>(out.data(), out.size()),
                                       ChannelLayout::Stereo,
                                       BufferLayout::Interleaved,
                                       frames)));
        REQUIRE(ChannelPeak(out, frames, 0) > 0.1F);
        for (float32 s : out)
        {
            REQUIRE(std::isfinite(s));
        }
    }
    SECTION("48000 source into 44100 session")
    {
        AudioSystem sys;
        InitOffline(sys, 44100);
        auto wav = test::MakeSineWav(48000, 1, 48000, 440.0, 0.5);
        ClipHandle clip = Prepare(sys, wav);
        VoiceHandle v{};
        REQUIRE(IsOk(sys.PlayClip({ .Clip = clip }, v)));
        constexpr uint32 frames = 4096;
        std::vector<float32> out(static_cast<std::size_t>(frames) * 2U);
        REQUIRE(IsOk(sys.RenderOffline(std::span<float32>(out.data(), out.size()),
                                       ChannelLayout::Stereo,
                                       BufferLayout::Interleaved,
                                       frames)));
        REQUIRE(ChannelPeak(out, frames, 0) > 0.1F);
        for (float32 s : out)
        {
            REQUIRE(std::isfinite(s));
        }
    }
}

TEST_CASE("Positional distance attenuation falls off with distance", "[audio][dsp]")
{
    auto peakAt = [](float32 dist) -> float32 {
        AudioSystem sys;
        InitOffline(sys);
        auto wav = test::MakeSineWav(48000, 1, 48000, 440.0, 0.5);
        ClipHandle clip = Prepare(sys, wav);

        // Listener at origin facing -Z.
        Command listen{};
        listen.Kind = CommandKind::SetListener;
        listen.Listener = ListenerPose{};
        REQUIRE(IsOk(sys.TrySubmitBatch(std::span<const Command>(&listen, 1)).Result));

        PlayParams p{};
        p.Clip = clip;
        p.Positional = true;
        p.MinDistance = 1.0F;
        p.MaxDistance = 100.0F;
        p.EmitterPosition = AudioVec3{dist, 0.0F, 0.0F};
        VoiceHandle v{};
        REQUIRE(IsOk(sys.PlayClip(p, v)));

        constexpr uint32 frames = 2048;
        std::vector<float32> out(static_cast<std::size_t>(frames) * 2U);
        REQUIRE(IsOk(sys.RenderOffline(std::span<float32>(out.data(), out.size()),
                                       ChannelLayout::Stereo,
                                       BufferLayout::Interleaved,
                                       frames)));
        float32 peak = 0.0F;
        for (uint32 f = 0; f < frames; ++f)
        {
            peak = std::max({peak,
                             std::fabs(out[static_cast<std::size_t>(f) * 2]),
                             std::fabs(out[static_cast<std::size_t>(f) * 2 + 1])});
        }
        return peak;
    };

    const float32 near = peakAt(1.0F); // within MinDistance => full
    const float32 mid = peakAt(50.0F);
    const float32 far = peakAt(100.0F); // at MaxDistance => ~0
    REQUIRE(near > mid);
    REQUIRE(mid > far);
    REQUIRE(far < 0.02F);
}

TEST_CASE("Invalid listener basis (NaN) is rejected", "[audio][dsp]")
{
    AudioSystem sys;
    InitOffline(sys);
    Command listen{};
    listen.Kind = CommandKind::SetListener;
    listen.Listener.Forward = AudioVec3{std::nanf(""), 0.0F, -1.0F};
    const BatchResult r = sys.TrySubmitBatch(std::span<const Command>(&listen, 1));
    REQUIRE(r.Result == Status::InvalidArgument);
}

TEST_CASE("Invalid positional distances are rejected", "[audio][dsp]")
{
    AudioSystem sys;
    InitOffline(sys);
    auto wav = test::MakeSineWav(48000, 1, 480);
    ClipHandle clip = Prepare(sys, wav);
    PlayParams p{};
    p.Clip = clip;
    p.Positional = true;
    p.MinDistance = 10.0F;
    p.MaxDistance = 5.0F; // <= MinDistance invalid
    VoiceHandle v{};
    REQUIRE(sys.PlayClip(p, v) == Status::InvalidArgument);
}
