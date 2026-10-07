#include "internal/document_history.h"

#include <catch2/catch_test_macros.hpp>

using ludus::editor::DocumentHistory;
using ludus::foundation::uint32;

TEST_CASE("Portable document history rejects stale heads and branches after Undo", "[editor][document]")
{
    DocumentHistory<uint32, 3> history;
    uint32 current = 1;
    REQUIRE(history.Commit(0, current));
    REQUIRE(history.Commit(current, 2));
    current = 2;
    const auto revision = history.Revision();
    uint32 stale = 9;
    CHECK_FALSE(history.Undo(stale));
    CHECK(stale == 9);
    CHECK(history.Revision() == revision);
    REQUIRE(history.Undo(current));
    CHECK(current == 1);
    CHECK(history.Revision() > revision);
    REQUIRE(history.Commit(current, 3));
    current = 3;
    CHECK_FALSE(history.CanRedo());
    REQUIRE(history.Undo(current));
    CHECK(current == 1);
    REQUIRE(history.Redo(current));
    CHECK(current == 3);
}

TEST_CASE("Portable history eviction retains content identity and bounded Undo", "[editor][document]")
{
    DocumentHistory<uint32, 2> history;
    constexpr uint32 SAVED = 0;
    uint32 current = SAVED;
    for (uint32 next = 1; next <= 3; ++next)
    {
        REQUIRE(history.Commit(current, next));
        current = next;
    }
    REQUIRE(history.Undo(current));
    REQUIRE(history.Undo(current));
    CHECK(current == 1);
    CHECK(current != SAVED);
    CHECK_FALSE(history.CanUndo());
    REQUIRE(history.Redo(current));
    REQUIRE(history.Redo(current));
    CHECK(current == 3);
    history.Reset();
    CHECK_FALSE(history.CanUndo());
    CHECK_FALSE(history.CanRedo());
    CHECK(history.Revision() == 0);
}

TEST_CASE("Portable history coalesces edits but preserves explicit save boundaries", "[editor][document]")
{
    DocumentHistory<uint32> history;
    uint32 current = 2;
    REQUIRE(history.Commit(0, 1));
    REQUIRE(history.Commit(1, current, true));
    REQUIRE(history.Undo(current));
    CHECK(current == 0);
    REQUIRE(history.Redo(current));
    CHECK(current == 2);
    history.BreakGroup();
    REQUIRE(history.Commit(current, 3, true));
    current = 3;
    REQUIRE(history.Undo(current));
    CHECK(current == 2);
    REQUIRE(history.Undo(current));
    CHECK(current == 0);
    history.Reset();
    REQUIRE(history.Commit(0, 1));
    REQUIRE(history.Commit(1, 0, true));
    CHECK_FALSE(history.CanUndo());
    CHECK_FALSE(history.CanRedo());
    CHECK(history.Revision() == 2);
    CHECK_FALSE(history.Commit(0, 0));
    CHECK(history.Revision() == 2);
}

TEST_CASE("Cancelling an edit group preserves earlier field boundaries", "[editor][document]")
{
    DocumentHistory<uint32> history;
    REQUIRE(history.Commit(0, 1)); // first field
    history.BreakGroup();
    REQUIRE(history.Commit(1, 2, true)); // second field
    REQUIRE(history.Commit(2, 1, true)); // cancel the second field's group
    REQUIRE(history.Commit(1, 3, true)); // continue editing the second field
    uint32 current = 3;
    REQUIRE(history.Undo(current));
    CHECK(current == 1);
    REQUIRE(history.Undo(current));
    CHECK(current == 0);
}
