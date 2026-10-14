// Header self-sufficiency and layout contract for Ludus::SceneFormat.

#include <ludus/content/scene_format/luscn.h>

#include <catch2/catch_test_macros.hpp>

namespace format = ludus::content::scene_format;

TEST_CASE("LUSCN record layouts match the wire format", "[scene_format]")
{
    REQUIRE(format::MAGIC.size() == 8);
    REQUIRE(format::MAGIC.back() == '\0');
    REQUIRE(sizeof(format::Header) == 68);
    REQUIRE(sizeof(format::VertexRecord) == 32);
    REQUIRE(sizeof(format::ObjectRecord) == 56);
    REQUIRE(format::NO_TEXTURE == 0xFFFFFFFFU);
}

TEST_CASE("Triangle bake charts are deterministic, guttered and capacity bounded", "[scene_format]")
{
    format::BakeChartLayout layout;
    REQUIRE(format::TryMakeBakeChartLayout(5, layout) == true);
    REQUIRE(layout.Width == 16);
    REQUIRE(layout.Height == 8);
    REQUIRE(layout.TriangleCount == 5);
    format::float32 u = -1;
    format::float32 v = -1;
    REQUIRE(format::TryGetBakeChartUv(layout, 4, 0, u, v) == true);
    REQUIRE(u == 1.5F / 16);
    REQUIRE(v == 5.5F / 8);
    REQUIRE(format::TryGetBakeChartUv(layout, 4, 1, u, v) == true);
    REQUIRE(u == 2.5F / 16);
    REQUIRE(format::TryGetBakeChartUv(layout, 4, 2, u, v) == true);
    REQUIRE(v == 6.5F / 8);
    REQUIRE(format::TryGetBakeChartUv(layout, 5, 0, u, v) == false);
    REQUIRE(u == 1.5F / 16);
    REQUIRE(v == 6.5F / 8);
    REQUIRE(format::TryMakeBakeChartLayout(0, layout) == false);
    REQUIRE(layout.TriangleCount == 5);
    constexpr format::uint32 MAX_TRIANGLES = (format::MAX_BAKE_CHART_DIMENSION / format::BAKE_CHART_TILE_PIXELS) *
                                             (format::MAX_BAKE_CHART_DIMENSION / format::BAKE_CHART_TILE_PIXELS);
    REQUIRE(format::TryMakeBakeChartLayout(MAX_TRIANGLES, layout) == true);
    REQUIRE(layout.Width == format::MAX_BAKE_CHART_DIMENSION);
    REQUIRE(layout.Height == format::MAX_BAKE_CHART_DIMENSION);
    REQUIRE(format::TryMakeBakeChartLayout(MAX_TRIANGLES + 1, layout) == false);
    REQUIRE(layout.TriangleCount == MAX_TRIANGLES);
}
