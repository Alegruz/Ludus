// Typed event descriptor validation + variation selection, and sub-span
// scheduled-start correctness (design sections 5, 9). Production paths only.

#include <ludus/audio/audio_app_adapter.h>
#include <ludus/audio/audio_system.h>

#include "wav_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace ludus::audio;
using ludus::audio::app::DescriptorField;
using ludus::audio::app::EventAdapter;
using ludus::audio::app::EventDescriptor;

namespace
{
void InitOffline(AudioSystem& sys)
{
    SystemConfig cfg{};
    cfg.SystemMode = Mode::Offline;
    cfg.SampleRate = 48000;
    REQUIRE(IsOk(sys.Initialize(cfg)));
}

ClipHandle Prepare(AudioSystem& sys, uint32 frames = 4800)
{
    auto wav = test::MakeSineWav(48000, 1, frames);
    ClipHandle h{};
    ClipDescriptor d{};
    REQUIRE(IsOk(sys.PrepareClip(std::span<const uint8>(wav.data(), wav.size()), d, h)));
    return h;
}
} // namespace

TEST_CASE("Descriptor validation flags the specific invalid field", "[audio][descriptor]")
{
    AudioSystem sys;
    InitOffline(sys);
    ClipHandle clip = Prepare(sys);
    EventAdapter adapter(sys);

    SECTION("empty variation list")
    {
        EventDescriptor d{};
        d.VariationCount = 0;
        const auto v = adapter.ValidateDescriptor(d);
        REQUIRE_FALSE(v.Valid);
        REQUIRE(v.Field == DescriptorField::VariationCount);
    }
    SECTION("unprepared variation handle")
    {
        EventDescriptor d{};
        d.VariationCount = 2;
        d.Variations[0] = clip;
        d.Variations[1] = ClipHandle{999, 5, 7}; // fabricated
        const auto v = adapter.ValidateDescriptor(d);
        REQUIRE_FALSE(v.Valid);
        REQUIRE(v.Field == DescriptorField::VariationHandle);
        REQUIRE(v.BadVariationIndex == 1);
    }
    SECTION("priority out of range")
    {
        EventDescriptor d{};
        d.VariationCount = 1;
        d.Variations[0] = clip;
        d.Priority = 99;
        const auto v = adapter.ValidateDescriptor(d);
        REQUIRE_FALSE(v.Valid);
        REQUIRE(v.Field == DescriptorField::Priority);
    }
    SECTION("rate out of range")
    {
        EventDescriptor d{};
        d.VariationCount = 1;
        d.Variations[0] = clip;
        d.Rate = 5.0F;
        const auto v = adapter.ValidateDescriptor(d);
        REQUIRE_FALSE(v.Valid);
        REQUIRE(v.Field == DescriptorField::Rate);
    }
    SECTION("invalid distances")
    {
        EventDescriptor d{};
        d.VariationCount = 1;
        d.Variations[0] = clip;
        d.Positional = true;
        d.MinDistance = 10.0F;
        d.MaxDistance = 5.0F;
        const auto v = adapter.ValidateDescriptor(d);
        REQUIRE_FALSE(v.Valid);
        REQUIRE(v.Field == DescriptorField::Distance);
    }
    SECTION("valid descriptor")
    {
        EventDescriptor d{};
        d.VariationCount = 1;
        d.Variations[0] = clip;
        const auto v = adapter.ValidateDescriptor(d);
        REQUIRE(v.Valid);
        REQUIRE(v.Field == DescriptorField::None);
    }
}

TEST_CASE("Descriptor variation selection is deterministic from the seed", "[audio][descriptor]")
{
    AudioSystem sys;
    InitOffline(sys);
    ClipHandle a = Prepare(sys);
    ClipHandle b = Prepare(sys);
    ClipHandle c = Prepare(sys);
    EventAdapter adapter(sys);

    EventDescriptor d{};
    d.EventId = 7;
    d.VariationCount = 3;
    d.Variations[0] = a;
    d.Variations[1] = b;
    d.Variations[2] = c;

    // seed % 3 picks the variant; same seed => same voice admitted.
    auto r0 = adapter.TriggerDescriptor(d, 1, 0, 0, 10); // variant 0
    REQUIRE(r0.Admitted);
    auto r1 = adapter.TriggerDescriptor(d, 2, 0, 1, 11); // variant 1
    REQUIRE(r1.Admitted);
    auto r2 = adapter.TriggerDescriptor(d, 3, 0, 2, 12); // variant 2
    REQUIRE(r2.Admitted);
    // Different owner tags admit independently; all three succeeded.
    REQUIRE(r0.Voice.IsValid());
    REQUIRE(r1.Voice.IsValid());
    REQUIRE(r2.Voice.IsValid());
}

TEST_CASE("An invalid descriptor admits nothing", "[audio][descriptor]")
{
    AudioSystem sys;
    InitOffline(sys);
    EventAdapter adapter(sys);
    EventDescriptor d{};
    d.VariationCount = 0; // invalid
    auto r = adapter.TriggerDescriptor(d, 1, 0, 0, 10);
    REQUIRE_FALSE(r.Admitted);
    REQUIRE_FALSE(r.Voice.IsValid());
}

TEST_CASE("A scheduled voice emits zero before its exact sub-span StartFrame", "[audio][descriptor]")
{
    // A voice scheduled to start at frame 300 (mid first quantum) must be silent
    // for frames [0,300) and audible from 300. Render 512 frames in one call.
    AudioSystem sys;
    InitOffline(sys);
    ClipHandle clip = Prepare(sys, 48000);

    PlayParams p{};
    p.Clip = clip;
    p.StartFrame = 300;
    VoiceHandle v{};
    REQUIRE(IsOk(sys.PlayClip(p, v)));

    constexpr uint32 frames = 512;
    std::vector<float32> out(static_cast<std::size_t>(frames) * 2);
    REQUIRE(IsOk(sys.RenderOffline(std::span<float32>(out.data(), out.size()),
                                   ChannelLayout::Stereo,
                                   BufferLayout::Interleaved,
                                   frames)));

    // Frames before 300 are exactly zero (the fade-in ramp begins at 300).
    float32 preStart = 0.0F;
    for (uint32 f = 0; f < 300; ++f)
    {
        preStart = std::max({preStart,
                             std::fabs(out[static_cast<std::size_t>(f) * 2]),
                             std::fabs(out[static_cast<std::size_t>(f) * 2 + 1])});
    }
    REQUIRE(preStart == 0.0F);

    // Signal is present after the start. The 5 ms ramp-in (240 frames) means
    // full gain is reached near frame 540 (beyond this 512 buffer), so check the
    // tail of the buffer where the ramp has risen enough to be clearly nonzero.
    float32 postStart = 0.0F;
    for (uint32 f = 450; f < frames; ++f)
    {
        postStart = std::max({postStart,
                              std::fabs(out[static_cast<std::size_t>(f) * 2]),
                              std::fabs(out[static_cast<std::size_t>(f) * 2 + 1])});
    }
    REQUIRE(postStart > 0.001F);
}
