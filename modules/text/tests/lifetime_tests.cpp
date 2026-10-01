// Ownership, lifetime, and handle-validation tests (requirements T03, T14):
// source-byte lifetime, font removal while layouts exist, stale/invalid
// handles, resource limits, partial creation, and bounded counters.

#include <ludus/text/font_system.h>

#include "fixture_support.h"

#include <catch2/catch_test_macros.hpp>

using namespace ludus::text;
using ludus::text::test::LatinFont;
using ludus::text::test::LoadFixtureFont;

namespace
{
struct SystemGuard
{
    FontSystem* System = nullptr;
    explicit SystemGuard(const FontSystemConfig& config = {})
    {
        REQUIRE(IsOk(CreateFontSystem(config, &System)));
    }
    ~SystemGuard()
    {
        (void)DestroyFontSystem(System);
    }
};
} // namespace

TEST_CASE("Null output and null system are rejected", "[text][lifetime]")
{
    CHECK(CreateFontSystem(FontSystemConfig{}, nullptr) == Status::InvalidArgument);
    // Destroying null is idempotent-OK.
    CHECK(IsOk(DestroyFontSystem(nullptr)));
}

TEST_CASE("Loaded bytes are copied, not retained", "[text][lifetime]")
{
    SystemGuard guard;
    FontId font;
    {
        // bytes goes out of scope before use; the system must own its own copy.
        auto bytes = LoadFixtureFont(LatinFont());
        REQUIRE_FALSE(bytes.empty());
        REQUIRE(IsOk(LoadFont(guard.System, bytes, 0, &font)));
        for (auto& b : bytes)
        {
            b = 0; // scribble the caller buffer
        }
    }
    // Shaping still works from the owned copy.
    LayoutId layout;
    CHECK(IsOk(ShapeLatin(guard.System, font, "ok", 24, &layout)));
}

TEST_CASE("Unloading a font with live layouts is Busy", "[text][lifetime]")
{
    SystemGuard guard;
    auto bytes = LoadFixtureFont(LatinFont());
    FontId font;
    REQUIRE(IsOk(LoadFont(guard.System, bytes, 0, &font)));

    LayoutId layout;
    REQUIRE(IsOk(ShapeLatin(guard.System, font, "x", 24, &layout)));
    CHECK(UnloadFont(guard.System, font) == Status::Busy);

    REQUIRE(IsOk(DestroyLayout(guard.System, layout)));
    CHECK(IsOk(UnloadFont(guard.System, font)));
}

TEST_CASE("Stale and invalid handles are rejected", "[text][lifetime]")
{
    SystemGuard guard;
    auto bytes = LoadFixtureFont(LatinFont());
    FontId font;
    REQUIRE(IsOk(LoadFont(guard.System, bytes, 0, &font)));
    LayoutId layout;
    REQUIRE(IsOk(ShapeLatin(guard.System, font, "x", 24, &layout)));

    const LayoutId stale = layout;
    REQUIRE(IsOk(DestroyLayout(guard.System, layout)));
    LayoutView view;
    CHECK(GetLayout(guard.System, stale, &view) == Status::InvalidHandle);
    CHECK(DestroyLayout(guard.System, stale) == Status::InvalidHandle);

    CHECK(GetLayout(guard.System, LayoutId{}, &view) == Status::InvalidHandle);
    CHECK(UnloadFont(guard.System, FontId{}) == Status::InvalidHandle);
}

TEST_CASE("Font count limit returns ResourceLimit", "[text][lifetime][limits]")
{
    FontSystemConfig config;
    config.MaxFonts = 2;
    SystemGuard guard(config);
    auto bytes = LoadFixtureFont(LatinFont());

    FontId a;
    FontId b;
    FontId c;
    REQUIRE(IsOk(LoadFont(guard.System, bytes, 0, &a)));
    REQUIRE(IsOk(LoadFont(guard.System, bytes, 0, &b)));
    CHECK(LoadFont(guard.System, bytes, 0, &c) == Status::ResourceLimit);
    CHECK_FALSE(c.IsValid());
}

TEST_CASE("Per-font byte limit is enforced before copying", "[text][lifetime][limits]")
{
    FontSystemConfig config;
    config.MaxFontBytesPerFont = 1024; // smaller than any fixture
    SystemGuard guard(config);
    auto bytes = LoadFixtureFont(LatinFont());
    FontId font;
    CHECK(LoadFont(guard.System, bytes, 0, &font) == Status::ResourceLimit);
}

TEST_CASE("Layout count limit returns ResourceLimit", "[text][lifetime][limits]")
{
    FontSystemConfig config;
    config.MaxLayouts = 2;
    SystemGuard guard(config);
    auto bytes = LoadFixtureFont(LatinFont());
    FontId font;
    REQUIRE(IsOk(LoadFont(guard.System, bytes, 0, &font)));

    LayoutId a;
    LayoutId b;
    LayoutId c;
    REQUIRE(IsOk(ShapeLatin(guard.System, font, "a", 16, &a)));
    REQUIRE(IsOk(ShapeLatin(guard.System, font, "b", 16, &b)));
    CHECK(ShapeLatin(guard.System, font, "c", 16, &c) == Status::ResourceLimit);
}

TEST_CASE("Run input byte limit returns ResourceLimit", "[text][lifetime][limits]")
{
    FontSystemConfig config;
    config.MaxRunInputBytes = 4;
    SystemGuard guard(config);
    auto bytes = LoadFixtureFont(LatinFont());
    FontId font;
    REQUIRE(IsOk(LoadFont(guard.System, bytes, 0, &font)));
    LayoutId layout;
    CHECK(ShapeLatin(guard.System, font, "toolong", 16, &layout) == Status::ResourceLimit);
}

TEST_CASE("Counters reflect loaded fonts, layouts, and shaping", "[text][lifetime][counters]")
{
    SystemGuard guard;
    auto bytes = LoadFixtureFont(LatinFont());
    FontId font;
    REQUIRE(IsOk(LoadFont(guard.System, bytes, 0, &font)));

    auto afterLoad = GetCounters(guard.System);
    CHECK(afterLoad.LoadedFonts == 1);
    CHECK(afterLoad.OwnedFontBytes == bytes.size());

    LayoutId layout;
    REQUIRE(IsOk(ShapeLatin(guard.System, font, "hello", 16, &layout)));
    auto afterShape = GetCounters(guard.System);
    CHECK(afterShape.LiveLayouts == 1);
    CHECK(afterShape.ShapeCalls == 1);
    CHECK(afterShape.RetainedGlyphRecords >= 5);

    REQUIRE(IsOk(DestroyLayout(guard.System, layout)));
    auto afterDestroy = GetCounters(guard.System);
    CHECK(afterDestroy.LiveLayouts == 0);
}

TEST_CASE("GetGlyphCount reports the face glyph count", "[text][lifetime]")
{
    SystemGuard guard;
    auto bytes = LoadFixtureFont(LatinFont());
    FontId font;
    REQUIRE(IsOk(LoadFont(guard.System, bytes, 0, &font)));
    ludus::foundation::uint32 count = 0;
    REQUIRE(IsOk(GetGlyphCount(guard.System, font, &count)));
    CHECK(count > 0);
}

TEST_CASE("Rejecting empty font bytes", "[text][lifetime]")
{
    SystemGuard guard;
    FontId font;
    std::span<const ludus::foundation::uint8> empty;
    CHECK(LoadFont(guard.System, empty, 0, &font) == Status::InvalidArgument);
}
