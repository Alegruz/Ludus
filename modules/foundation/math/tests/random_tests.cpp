// PCG32 known-answer sequence, bounded sampling edges, float mapping, state
// save/restore and non-consumption on invalid input.

#include <ludus/foundation/math/random.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation::math;

TEST_CASE("PCG32 known-answer sequence for seed=42 selector=54", "[math][random]")
{
    RandomStream rng;
    REQUIRE(rng.TryReseed(42, 54));
    const uint32 expected[6] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (uint32 e : expected)
    {
        REQUIRE(rng.NextUInt32() == e);
    }
}

TEST_CASE("Bounded sampling: bound=1 is always 0; bound=0 is invalid and non-consuming",
          "[math][random]")
{
    RandomStream rng;
    const RandomState before = rng.GetState();
    uint32 out = 99u;
    REQUIRE(rng.TryNextBounded(0u, out) == MathStatus::InvalidArgument);
    REQUIRE(out == 99u); // output unchanged
    const RandomState after = rng.GetState();
    REQUIRE(before.State == after.State);
    REQUIRE(before.Increment == after.Increment); // stream unchanged

    REQUIRE(rng.TryNextBounded(1u, out) == MathStatus::Success);
    REQUIRE(out == 0u);
}

TEST_CASE("Bounded sampling stays within range for large bounds", "[math][random]")
{
    RandomStream rng;
    REQUIRE(rng.TryReseed(7, 1));
    for (int i = 0; i < 10000; ++i)
    {
        uint32 out = 0u;
        REQUIRE(rng.TryNextBounded(1000u, out) == MathStatus::Success);
        REQUIRE(out < 1000u);
    }
    // Bound = UINT32_MAX still works.
    uint32 out = 0u;
    REQUIRE(rng.TryNextBounded(0xffffffffu, out) == MathStatus::Success);
    REQUIRE(out < 0xffffffffu);
}

TEST_CASE("NextFloat01 is in [0,1) deterministically", "[math][random]")
{
    RandomStream rng;
    for (int i = 0; i < 100000; ++i)
    {
        const float f = rng.NextFloat01();
        REQUIRE(f >= 0.0f);
        REQUIRE(f < 1.0f);
    }
}

TEST_CASE("State save/restore replay is bit-exact; bad state rejected non-destructively",
          "[math][random]")
{
    RandomStream rng;
    (void)rng.NextUInt32();
    const RandomState saved = rng.GetState();
    const uint32 a = rng.NextUInt32();
    const uint32 b = rng.NextUInt32();

    RandomStream replay;
    REQUIRE(replay.TryRestore(saved) == MathStatus::Success);
    REQUIRE(replay.NextUInt32() == a);
    REQUIRE(replay.NextUInt32() == b);

    // Even increment rejected without changing the stream.
    RandomState evenInc = saved;
    evenInc.Increment &= ~1ull;
    const RandomState pre = replay.GetState();
    REQUIRE(replay.TryRestore(evenInc) == MathStatus::InvalidArgument);
    REQUIRE(replay.GetState().State == pre.State);
    REQUIRE(replay.GetState().Increment == pre.Increment);

    // Wrong version rejected.
    RandomState wrongVersion = saved;
    wrongVersion.Version = 2;
    REQUIRE(replay.TryRestore(wrongVersion) == MathStatus::InvalidArgument);
}

TEST_CASE("Selector above 2^63-1 is rejected and leaves the stream unchanged", "[math][random]")
{
    RandomStream rng;
    const RandomState before = rng.GetState();
    REQUIRE_FALSE(rng.TryReseed(1, 0x8000000000000000ull));
    const RandomState after = rng.GetState();
    REQUIRE(before.State == after.State);
    REQUIRE(before.Increment == after.Increment);
}
