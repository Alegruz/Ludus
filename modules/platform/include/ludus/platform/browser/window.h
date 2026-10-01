#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::platform::browser
{
using ludus::foundation::float64;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;
using ludus::foundation::usize;

// Physical KeyboardEvent.code positions, not text input or a locale layout.
// Unsupported codes produce Unknown events and never index the held-key table.
enum class Key : uint8
{
    Unknown,
    KeyA,
    KeyB,
    KeyC,
    KeyD,
    KeyE,
    KeyF,
    KeyG,
    KeyH,
    KeyI,
    KeyJ,
    KeyK,
    KeyL,
    KeyM,
    KeyN,
    KeyO,
    KeyP,
    KeyQ,
    KeyR,
    KeyS,
    KeyT,
    KeyU,
    KeyV,
    KeyW,
    KeyX,
    KeyY,
    KeyZ,
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    ArrowLeft,
    ArrowRight,
    ArrowUp,
    ArrowDown,
    Space,
    Enter,
    Escape,
    Tab,
    Backspace,
    ShiftLeft,
    ShiftRight,
    ControlLeft,
    ControlRight,
    AltLeft,
    AltRight,
    MetaLeft,
    MetaRight,
    Delete,
    Insert,
    Home,
    End,
    PageUp,
    PageDown,
    Count,
};

enum class EventKind : uint8
{
    Resize,
    Focus,
    Visibility,
    KeyDown,
    KeyUp,
    PointerMove,
    PointerDown,
    PointerUp,
    Wheel,
    InputReset,
    PointerCancel,
};

struct InputEvent final
{
    EventKind Kind = EventKind::InputReset;
    Key PhysicalKey = Key::Unknown;
    bool Repeat = false;
    uint32 Buttons = 0;
    float64 X = 0; // canvas CSS coordinates, not framebuffer pixels
    float64 Y = 0;
    float64 WheelX = 0; // normalized CSS pixels (line=16px, page=canvas height)
    float64 WheelY = 0;
};

// Authoritative main-thread snapshot. Held input is cleared on blur, hidden
// page/canvas and removal; pointer cancellation clears buttons only. Event overflow never prevents
// state updates; consumers must use the snapshot after DroppedEvents increases.
struct WindowState final
{
    bool Attached = false;
    bool Focused = false;
    bool Visible = false;
    float64 CssWidth = 0;
    float64 CssHeight = 0;
    float64 PixelRatio = 1;
    uint32 FramebufferWidth = 0;
    uint32 FramebufferHeight = 0;
    bool Keys[static_cast<usize>(Key::Count)] = {};
    uint32 Buttons = 0;
    float64 PointerX = 0;
    float64 PointerY = 0;
    uint64 Revision = 0;
    uint64 DroppedEvents = 0;
};
} // namespace ludus::platform::browser
