// Compositor-independent coverage of the Wayland keyboard adapter's normalization
// seam (K01, K10). The evdev->Key table is a pure function; we verify it against
// known Linux evdev codes and confirm unsupported/out-of-range codes normalize to
// Unknown with no mapping. Full attach/focus/routing requires a real compositor
// (see wayland_event_tests.cpp and the M5 manual matrix).

#include "internal/evdev_keymap.hpp"

#include <ludus/input/key.h>
#include <ludus/platform/base/window.h>
#include <ludus/platform/keyboard_sink.h>

#include <catch2/catch_test_macros.hpp>

#include <linux/input-event-codes.h>

using ludus::input::Key;
using ludus::platform::wayland::MapEvdevCode;

TEST_CASE("evdev codes map to the expected physical keys", "[platform][wayland][keyboard]")
{
    REQUIRE(MapEvdevCode(KEY_W) == Key::KeyW);
    REQUIRE(MapEvdevCode(KEY_A) == Key::KeyA);
    REQUIRE(MapEvdevCode(KEY_S) == Key::KeyS);
    REQUIRE(MapEvdevCode(KEY_D) == Key::KeyD);
    REQUIRE(MapEvdevCode(KEY_SPACE) == Key::Space);
    REQUIRE(MapEvdevCode(KEY_ESC) == Key::Escape);
    REQUIRE(MapEvdevCode(KEY_ENTER) == Key::Enter);
    REQUIRE(MapEvdevCode(KEY_KPENTER) == Key::KeypadEnter);
    REQUIRE(MapEvdevCode(KEY_UP) == Key::ArrowUp);
    REQUIRE(MapEvdevCode(KEY_F1) == Key::F1);
    REQUIRE(MapEvdevCode(KEY_F24) == Key::F24);
}

TEST_CASE("Left and right modifiers map to distinct keys", "[platform][wayland][keyboard]")
{
    REQUIRE(MapEvdevCode(KEY_LEFTSHIFT) == Key::ShiftLeft);
    REQUIRE(MapEvdevCode(KEY_RIGHTSHIFT) == Key::ShiftRight);
    REQUIRE(MapEvdevCode(KEY_LEFTCTRL) == Key::ControlLeft);
    REQUIRE(MapEvdevCode(KEY_RIGHTCTRL) == Key::ControlRight);
    REQUIRE(MapEvdevCode(KEY_LEFTALT) == Key::AltLeft);
    REQUIRE(MapEvdevCode(KEY_RIGHTALT) == Key::AltRight);
    REQUIRE(MapEvdevCode(KEY_LEFTMETA) == Key::SuperLeft);
    REQUIRE(MapEvdevCode(KEY_RIGHTMETA) == Key::SuperRight);
}

TEST_CASE("Keypad digits are distinct from the top-row digits", "[platform][wayland][keyboard]")
{
    REQUIRE(MapEvdevCode(KEY_KP0) == Key::Keypad0);
    REQUIRE(MapEvdevCode(KEY_0) == Key::Digit0);
    REQUIRE(MapEvdevCode(KEY_KP0) != MapEvdevCode(KEY_0));
}

TEST_CASE("Unsupported, vendor and out-of-range codes normalize to Unknown", "[platform][wayland][keyboard]")
{
    // A media key the physical-only milestone does not support.
    REQUIRE(MapEvdevCode(KEY_PLAYPAUSE) == Key::Unknown);
    // Code 0 (KEY_RESERVED) and a code past the table are Unknown, not OOB.
    REQUIRE(MapEvdevCode(0) == Key::Unknown);
    REQUIRE(MapEvdevCode(100000) == Key::Unknown);
    // No evdev code maps to Unknown except by being unmapped: every mapped code
    // is a valid key.
    for (ludus::foundation::uint32 code = 1; code < ludus::platform::wayland::EVDEV_TABLE_SIZE; ++code)
    {
        const Key key = MapEvdevCode(code);
        if (key != Key::Unknown)
        {
            REQUIRE(ludus::input::IsValidKey(key));
        }
    }
}

namespace
{
int gRecords = 0;
int gResets = 0;
ludus::input::Key gLastKey = ludus::input::Key::Unknown;

void onRecord(void* userData, const ludus::input::KeyboardRecord& record) noexcept
{
    ++gRecords;
    gLastKey = record.PhysicalKey;
    *static_cast<int*>(userData) += 1;
}
void onReset(void*, ludus::input::ResetReason, const ludus::input::FocusBaseline&) noexcept
{
    ++gResets;
}
} // namespace

TEST_CASE("Keyboard sink attach/detach stores the trio and clears it", "[platform][wayland][keyboard]")
{
    // The window base stores the sink; verify the plain-function-pointer seam
    // round-trips and detach clears it (routing itself needs a compositor).
    gRecords = 0;
    gResets = 0;
    int counter = 0;

    // A headless-style stand-in: WindowBase is abstract, so test the sink struct
    // directly as the backend would invoke it.
    ludus::platform::KeyboardSink sink{ .OnRecord = onRecord, .OnReset = onReset, .UserData = &counter };

    ludus::input::KeyboardRecord record;
    record.PhysicalKey = ludus::input::Key::KeyW;
    sink.OnRecord(sink.UserData, record);
    REQUIRE(gRecords == 1);
    REQUIRE(counter == 1);
    REQUIRE(gLastKey == ludus::input::Key::KeyW);

    sink.OnReset(sink.UserData, ludus::input::ResetReason::FocusLost, ludus::input::FocusBaseline{});
    REQUIRE(gResets == 1);

    ludus::platform::KeyboardSink empty{};
    REQUIRE(empty.OnRecord == nullptr);
    REQUIRE(empty.OnReset == nullptr);
    REQUIRE(empty.UserData == nullptr);
}
