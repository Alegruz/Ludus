#include "internal/cocoa_keymap.hpp"

#include <catch2/catch_test_macros.hpp>

using ludus::input::Key;
using ludus::platform::cocoa::MapKeyCode;
using ludus::platform::cocoa::ModifierMask;

TEST_CASE("Cocoa virtual codes map physical positions rather than typed characters", "[platform][input]")
{
    CHECK(MapKeyCode(0x00) == Key::KeyA);
    CHECK(MapKeyCode(0x0D) == Key::KeyW);
    CHECK(MapKeyCode(0x01) == Key::KeyS);
    CHECK(MapKeyCode(0x02) == Key::KeyD);
    CHECK(MapKeyCode(0x12) == Key::Digit1);
    CHECK(MapKeyCode(0x53) == Key::Keypad1);
    CHECK(MapKeyCode(0x24) == Key::Enter);
    CHECK(MapKeyCode(0x4C) == Key::KeypadEnter);
    CHECK(MapKeyCode(0x33) == Key::Backspace);
    CHECK(MapKeyCode(0x75) == Key::Delete);
    CHECK(MapKeyCode(0x35) == Key::Escape);
    CHECK(MapKeyCode(0x7E) == Key::ArrowUp);
    CHECK(MapKeyCode(0x7B) == Key::ArrowLeft);
    CHECK(MapKeyCode(0x7A) == Key::F1);
    CHECK(MapKeyCode(0x5A) == Key::F20);
}

TEST_CASE("Cocoa distinguishes both modifier sides and physical Caps Lock", "[platform][input]")
{
    CHECK(MapKeyCode(0x38) == Key::ShiftLeft);
    CHECK(MapKeyCode(0x3C) == Key::ShiftRight);
    CHECK(MapKeyCode(0x3B) == Key::ControlLeft);
    CHECK(MapKeyCode(0x3E) == Key::ControlRight);
    CHECK(MapKeyCode(0x3A) == Key::AltLeft);
    CHECK(MapKeyCode(0x3D) == Key::AltRight);
    CHECK(MapKeyCode(0x37) == Key::SuperLeft);
    CHECK(MapKeyCode(0x36) == Key::SuperRight);
    CHECK(ModifierMask(Key::ShiftLeft) == 0x02);
    CHECK(ModifierMask(Key::ShiftRight) == 0x04);
    CHECK(ModifierMask(Key::ControlRight) == 0x2000);
    CHECK(ModifierMask(Key::CapsLock) == 0x01000000);
    CHECK(ModifierMask(Key::KeyA) == 0);
}

TEST_CASE("Unsupported and out-of-range Cocoa keys cannot index input storage", "[platform][input]")
{
    CHECK(MapKeyCode(0x3F) == Key::Unknown); // Fn
    CHECK(MapKeyCode(0x48) == Key::Unknown); // volume up
    CHECK(MapKeyCode(0x51) == Key::Unknown); // keypad equals has no Ludus key
    CHECK(MapKeyCode(128) == Key::Unknown);
    CHECK(MapKeyCode(0xFFFFFFFF) == Key::Unknown);
    for (ludus::foundation::uint32 code = 0; code < 128; ++code)
    {
        const Key key = MapKeyCode(code);
        CHECK((key == Key::Unknown || ludus::input::IsValidKey(key)));
    }
}
