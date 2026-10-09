#include <catch2/catch_test_macros.hpp>
#include <ludus/graphics/text/coverage_atlas.hpp>
TEST_CASE("Coverage atlas reserves transparent gutters and preserves cells on pressure", "[atlas]")
{
    using namespace ludus::foundation;
    namespace gt = ludus::graphics::text;
    using Status = ludus::graphics::rhi::RasterStatus;
    gt::CoverageAtlas atlas;
    REQUIRE(atlas.Initialize() == Status::Ready);
    const uint8 mask[]{255, 127, 64, 0};
    gt::AtlasGlyph glyph;
    REQUIRE(atlas.Insert({1, 42, 16}, mask, 4, 2, 2, -1, 3, glyph) == Status::Ready);
    CHECK(atlas.Coverage()[0] == 0);
    CHECK(atlas.Coverage()[257] == 255);
    CHECK(atlas.Coverage()[258] == 127);
    CHECK(atlas.Coverage()[259] == 0);
    CHECK(atlas.OccupiedTexels() == 16);
    CHECK(glyph.BearingLeft == -1);
    REQUIRE(atlas.Insert({1, 42, 16}, mask, 4, 2, 2, 99, 99, glyph) == Status::Ready);
    CHECK(atlas.GlyphCount() == 1);
    CHECK(glyph.BearingLeft == -1);
    for (uint32 i = 0; i < 511; ++i)
    {
        REQUIRE(atlas.Insert({2, i, 16}, nullptr, 0, 0, 0, 0, 0, glyph) == Status::Ready);
    }
    CHECK(atlas.Insert({3, 1, 16}, mask, 4, 2, 2, 0, 0, glyph) == Status::CapacityExceeded);
    CHECK(atlas.Coverage()[257] == 255);
    CHECK(atlas.GlyphCount() == 512);
    atlas.Reset();
    CHECK(atlas.Coverage()[257] == 0);
    CHECK(atlas.GlyphCount() == 0);
}
#include "fixture_support.h"
#include <ludus/graphics/text/text_adapter.hpp>
TEST_CASE("CPU shaped Latin Hangul and Arabic runs retain clusters and cache warm masks", "[atlas][text]")
{
    namespace cpu = ludus::text;
    namespace gt = ludus::graphics::text;
    using Status = ludus::graphics::rhi::RasterStatus;
    using namespace ludus::foundation;
    cpu::FontSystem* system = nullptr;
    REQUIRE(cpu::CreateFontSystem({}, &system) == cpu::Status::Ok);
    struct Guard
    {
        cpu::FontSystem* System;
        ~Guard()
        {
            static_cast<void>(cpu::DestroyFontSystem(System));
        }
    } guard{system};
    const char* fonts[]{cpu::test::LatinFont(), cpu::test::HangulFont(), cpu::test::ArabicFont()};
    const char* labels[]{"AV office", "한글", "مرحبا"};
    const char* scripts[]{"Latn", "Hang", "Arab"};
    gt::CoverageAtlas atlas;
    REQUIRE(atlas.Initialize() == Status::Ready);
    for (usize run = 0; run < 3; ++run)
    {
        auto bytes = cpu::test::LoadFixtureFont(fonts[run]);
        REQUIRE(!bytes.empty());
        cpu::FontId font;
        REQUIRE(cpu::LoadFont(system, bytes, 0, &font) == cpu::Status::Ok);
        cpu::RunSpec spec;
        spec.PixelHeight = 16;
        for (usize i = 0; i < 4; ++i)
        {
            spec.Script[i] = scripts[run][i];
        }
        if (run == 2)
        {
            spec.TextDirection = cpu::Direction::RightToLeft;
        }
        cpu::LayoutId layout;
        REQUIRE(cpu::ShapeRun(system, font, labels[run], spec, &layout) == cpu::Status::Ok);
        cpu::LayoutView view;
        REQUIRE(cpu::GetLayout(system, layout, &view) == cpu::Status::Ok);
        gt::TextQuad quads[64];
        usize count = 99;
        REQUIRE(gt::PrepareText(atlas, system, layout, run + 1, quads, 64, count) == Status::Ready);
        CHECK(count == view.Glyphs.size());
        for (usize i = 0; i < count; ++i)
        {
            CHECK(quads[i].Cluster == view.Glyphs[i].Cluster);
        }
        const auto rasterCalls = cpu::GetCounters(system).RasterizeCalls;
        REQUIRE(gt::PrepareText(atlas, system, layout, run + 1, quads, 64, count) == Status::Ready);
        CHECK(cpu::GetCounters(system).RasterizeCalls == rasterCalls);
        CHECK(cpu::GetCounters(system).ShapeCalls == run + 1);
        ludus::graphics::renderer::OverlayList list;
        REQUIRE(list.Initialize() == Status::Ready);
        REQUIRE(gt::AppendText(list, quads, count, {5, 24}, {1, 1, 1, 1}, 96, 64) == Status::Ready);
        CHECK(list.VertexCount() > 0);
        REQUIRE(cpu::DestroyLayout(system, layout) == cpu::Status::Ok);
        REQUIRE(cpu::UnloadFont(system, font) == cpu::Status::Ok);
    }
}
