// Handle / session / generation contract tests (design section 3; tasks A1 gate
// rows "Invalid/cross-system/stale/fabricated handle" and "Generation/session/
// epoch exhaustion").

#include "internal/handles.hpp"

#include <ludus/audio/audio_types.h>

#include <catch2/catch_test_macros.hpp>

using ludus::audio::ClipHandle;
using ludus::audio::VoiceHandle;
using ludus::audio::internal::SlotGeneration;
using ludus::foundation::uint32;

TEST_CASE("Zero session or generation is an invalid handle", "[audio][handle]")
{
    REQUIRE_FALSE(VoiceHandle{}.IsValid());
    REQUIRE_FALSE((VoiceHandle{0, 5, 7}).IsValid());
    REQUIRE_FALSE((VoiceHandle{3, 5, 0}).IsValid());
    REQUIRE((VoiceHandle{1, 0, 1}).IsValid());
}

TEST_CASE("Session source yields unique nonzero ids and never reuses one", "[audio][handle]")
{
    const uint32 a = ludus::audio::internal::SessionSource::Next();
    const uint32 b = ludus::audio::internal::SessionSource::Next();
    const uint32 c = ludus::audio::internal::SessionSource::Next();
    REQUIRE(a != 0);
    REQUIRE(b == a + 1);
    REQUIRE(c == b + 1);
}

TEST_CASE("Slot generation advances without wrap and reports exhaustion", "[audio][handle]")
{
    SlotGeneration g;
    REQUIRE(g.Value == 0);
    REQUIRE(g.Advance());
    REQUIRE(g.Value == 1);
    REQUIRE(g.Advance());
    REQUIRE(g.Value == 2);
    REQUIRE_FALSE(g.IsExhausted());

    // Drive to the sentinel boundary.
    g.Value = SlotGeneration::kExhausted - 1U;
    REQUIRE_FALSE(g.Advance()); // reaching the sentinel is exhaustion
    REQUIRE(g.IsExhausted());
    REQUIRE_FALSE(g.Advance()); // stays exhausted
}

TEST_CASE("Handle equality compares all three identity fields", "[audio][handle]")
{
    REQUIRE((ClipHandle{1, 2, 3}) == (ClipHandle{1, 2, 3}));
    REQUIRE_FALSE((ClipHandle{1, 2, 3}) == (ClipHandle{1, 2, 4}));
    REQUIRE_FALSE((ClipHandle{1, 2, 3}) == (ClipHandle{2, 2, 3}));
}
