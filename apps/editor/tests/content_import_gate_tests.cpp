#include "internal/content_import_gate.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Import cancellation wins before publication and acknowledges repeated requests", "[editor][content]")
{
    ludus::editor::ContentImportGate gate;
    REQUIRE(gate.Cancel());
    CHECK(gate.Cancelled());
    CHECK(gate.Cancel());
    CHECK_FALSE(gate.Publish());
}
TEST_CASE("Late cancellation cannot reverse a begun publication", "[editor][content]")
{
    ludus::editor::ContentImportGate gate;
    REQUIRE(gate.Publish());
    CHECK_FALSE(gate.Cancel());
    CHECK_FALSE(gate.Cancelled());
    CHECK_FALSE(gate.Publish());
}
