#include <ludus/input/key.h>

#include <catch2/catch_test_macros.hpp>

#include <cstring>

using ludus::input::DebugLabel;
using ludus::input::IsValidKey;
using ludus::input::Key;
using ludus::input::KEY_COUNT;
using ludus::input::KeyIndex;

TEST_CASE("Unknown is reserved and never valid for indexing", "[input][key]")
{
    REQUIRE_FALSE(IsValidKey(Key::Unknown));
    REQUIRE(std::strcmp(DebugLabel(Key::Unknown), "Unknown") == 0);
}

TEST_CASE("Every real key validates, indexes in range, and has a label", "[input][key]")
{
    for (ludus::foundation::uint16 raw = 1; raw < static_cast<ludus::foundation::uint16>(Key::Count); ++raw)
    {
        const Key key = static_cast<Key>(raw);
        REQUIRE(IsValidKey(key));
        REQUIRE(KeyIndex(key) < KEY_COUNT);
        REQUIRE(DebugLabel(key) != nullptr);
        // A real key must not report the Unknown label.
        REQUIRE(std::strcmp(DebugLabel(key), "Unknown") != 0);
    }
}

TEST_CASE("Fabricated out-of-range enum values are rejected", "[input][key]")
{
    const Key fabricated = static_cast<Key>(static_cast<ludus::foundation::uint16>(Key::Count));
    const Key wayOut = static_cast<Key>(50000);
    REQUIRE_FALSE(IsValidKey(fabricated));
    REQUIRE_FALSE(IsValidKey(wayOut));
    // DebugLabel never reads out of bounds for a fabricated value.
    REQUIRE(std::strcmp(DebugLabel(fabricated), "Unknown") == 0);
    REQUIRE(std::strcmp(DebugLabel(wayOut), "Unknown") == 0);
}

TEST_CASE("Left/right modifiers and keypad digits are distinct", "[input][key]")
{
    REQUIRE(Key::ShiftLeft != Key::ShiftRight);
    REQUIRE(Key::ControlLeft != Key::ControlRight);
    REQUIRE(Key::AltLeft != Key::AltRight);
    REQUIRE(Key::SuperLeft != Key::SuperRight);
    REQUIRE(Key::Keypad0 != Key::Digit0);
    REQUIRE(Key::KeypadEnter != Key::Enter);
}
