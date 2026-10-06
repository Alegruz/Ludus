#pragma once

#include <ludus/foundation/base/types.h>
#include <ludus/input/key.h>

// Thanks to Apple, HIToolbox/Events.h (macOS SDK), virtual-key position constants,
// and IOKit/hidsystem/IOLLEvent.h, device-specific modifier flags. This table
// adapts those native positions to Ludus keys without translating characters.
// NSEvent keyCode contract: https://developer.apple.com/documentation/appkit/nsevent/keycode
// Unmapped ISO/JIS/media/Fn/keypad-equals positions stay Unknown; no layout/text inference.
namespace ludus::platform::cocoa
{
using foundation::uint32;
using foundation::uint64;
using input::Key;
inline constexpr uint32 KEYCODE_COUNT = 128;
struct KeyTable final
{
    Key Entries[KEYCODE_COUNT] = {};
    constexpr KeyTable() noexcept
    {
        Entries[0x00] = Key::KeyA;
        Entries[0x01] = Key::KeyS;
        Entries[0x02] = Key::KeyD;
        Entries[0x03] = Key::KeyF;
        Entries[0x04] = Key::KeyH;
        Entries[0x05] = Key::KeyG;
        Entries[0x06] = Key::KeyZ;
        Entries[0x07] = Key::KeyX;
        Entries[0x08] = Key::KeyC;
        Entries[0x09] = Key::KeyV;
        Entries[0x0B] = Key::KeyB;
        Entries[0x0C] = Key::KeyQ;
        Entries[0x0D] = Key::KeyW;
        Entries[0x0E] = Key::KeyE;
        Entries[0x0F] = Key::KeyR;
        Entries[0x10] = Key::KeyY;
        Entries[0x11] = Key::KeyT;
        Entries[0x12] = Key::Digit1;
        Entries[0x13] = Key::Digit2;
        Entries[0x14] = Key::Digit3;
        Entries[0x15] = Key::Digit4;
        Entries[0x16] = Key::Digit6;
        Entries[0x17] = Key::Digit5;
        Entries[0x18] = Key::Equal;
        Entries[0x19] = Key::Digit9;
        Entries[0x1A] = Key::Digit7;
        Entries[0x1B] = Key::Minus;
        Entries[0x1C] = Key::Digit8;
        Entries[0x1D] = Key::Digit0;
        Entries[0x1E] = Key::BracketRight;
        Entries[0x1F] = Key::KeyO;
        Entries[0x20] = Key::KeyU;
        Entries[0x21] = Key::BracketLeft;
        Entries[0x22] = Key::KeyI;
        Entries[0x23] = Key::KeyP;
        Entries[0x24] = Key::Enter;
        Entries[0x25] = Key::KeyL;
        Entries[0x26] = Key::KeyJ;
        Entries[0x27] = Key::Quote;
        Entries[0x28] = Key::KeyK;
        Entries[0x29] = Key::Semicolon;
        Entries[0x2A] = Key::Backslash;
        Entries[0x2B] = Key::Comma;
        Entries[0x2C] = Key::Slash;
        Entries[0x2D] = Key::KeyN;
        Entries[0x2E] = Key::KeyM;
        Entries[0x2F] = Key::Period;
        Entries[0x30] = Key::Tab;
        Entries[0x31] = Key::Space;
        Entries[0x32] = Key::Backquote;
        Entries[0x33] = Key::Backspace;
        Entries[0x35] = Key::Escape;
        Entries[0x36] = Key::SuperRight;
        Entries[0x37] = Key::SuperLeft;
        Entries[0x38] = Key::ShiftLeft;
        Entries[0x39] = Key::CapsLock;
        Entries[0x3A] = Key::AltLeft;
        Entries[0x3B] = Key::ControlLeft;
        Entries[0x3C] = Key::ShiftRight;
        Entries[0x3D] = Key::AltRight;
        Entries[0x3E] = Key::ControlRight;
        Entries[0x40] = Key::F17;
        Entries[0x41] = Key::KeypadDecimal;
        Entries[0x43] = Key::KeypadMultiply;
        Entries[0x45] = Key::KeypadAdd;
        Entries[0x4B] = Key::KeypadDivide;
        Entries[0x4C] = Key::KeypadEnter;
        Entries[0x4E] = Key::KeypadSubtract;
        Entries[0x4F] = Key::F18;
        Entries[0x50] = Key::F19;
        Entries[0x52] = Key::Keypad0;
        Entries[0x53] = Key::Keypad1;
        Entries[0x54] = Key::Keypad2;
        Entries[0x55] = Key::Keypad3;
        Entries[0x56] = Key::Keypad4;
        Entries[0x57] = Key::Keypad5;
        Entries[0x58] = Key::Keypad6;
        Entries[0x59] = Key::Keypad7;
        Entries[0x5A] = Key::F20;
        Entries[0x5B] = Key::Keypad8;
        Entries[0x5C] = Key::Keypad9;
        Entries[0x60] = Key::F5;
        Entries[0x61] = Key::F6;
        Entries[0x62] = Key::F7;
        Entries[0x63] = Key::F3;
        Entries[0x64] = Key::F8;
        Entries[0x65] = Key::F9;
        Entries[0x67] = Key::F11;
        Entries[0x69] = Key::F13;
        Entries[0x6A] = Key::F16;
        Entries[0x6B] = Key::F14;
        Entries[0x6D] = Key::F10;
        Entries[0x6E] = Key::Menu;
        Entries[0x6F] = Key::F12;
        Entries[0x71] = Key::F15;
        Entries[0x72] = Key::Insert;
        Entries[0x73] = Key::Home;
        Entries[0x74] = Key::PageUp;
        Entries[0x75] = Key::Delete;
        Entries[0x76] = Key::F4;
        Entries[0x77] = Key::End;
        Entries[0x78] = Key::F2;
        Entries[0x79] = Key::PageDown;
        Entries[0x7A] = Key::F1;
        Entries[0x7B] = Key::ArrowLeft;
        Entries[0x7C] = Key::ArrowRight;
        Entries[0x7D] = Key::ArrowDown;
        Entries[0x7E] = Key::ArrowUp;
    }
};
inline constexpr KeyTable KEY_TABLE;
constexpr Key MapKeyCode(uint32 code) noexcept
{
    return code < KEYCODE_COUNT ? KEY_TABLE.Entries[code] : Key::Unknown;
}
constexpr uint64 ModifierMask(Key key) noexcept
{
    switch (key)
    {
        case Key::ControlLeft:
            return 0x00000001;
        case Key::ShiftLeft:
            return 0x00000002;
        case Key::ShiftRight:
            return 0x00000004;
        case Key::SuperLeft:
            return 0x00000008;
        case Key::SuperRight:
            return 0x00000010;
        case Key::AltLeft:
            return 0x00000020;
        case Key::AltRight:
            return 0x00000040;
        case Key::ControlRight:
            return 0x00002000;
        // Physical Caps Lock, distinct from the persistent Caps Lock latch.
        case Key::CapsLock:
            return 0x01000000;
        default:
            return 0;
    }
}
} // namespace ludus::platform::cocoa
