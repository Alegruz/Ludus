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
