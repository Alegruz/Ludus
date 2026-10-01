#pragma once

// Ludus physical key vocabulary (K01, K04).
//
// `Key` identifies a physical key *position*, not a character and not a native
// scan code. Its values are Ludus-owned and stable; they are neither evdev
// codes, XKB keysyms, USB usages, nor Unicode. Names such as KeyW/KeyA mean the
// positions bearing those labels on a US reference layout — they do NOT claim to
// match the user's active layout, and this header intentionally exposes no text
// or layout-display API (those are deferred).
//
// `Unknown` (value 0) is reserved for unsupported media/vendor keys and for any
// native code the adapter cannot map; such records never index key storage and
// never change gameplay state. `Count` is the one-past-the-end sentinel: dense
// key storage is sized to `Count`. Every externally supplied value — including
// a fabricated out-of-range enum value or a negative native code — MUST be
// validated with IsValidKey() before it is used to index a mask.

#include <ludus/foundation/base/types.h>

namespace ludus::input
{
using ludus::foundation::uint16;
using ludus::foundation::usize;

// Scoped physical-key enum. Explicit, stable values; Unknown reserved at 0.
// Left/right modifier pairs and the keypad are kept distinct (K01).
//
// The underlying type is fixed at uint16 by design (spec design section 2): the
// Ludus key domain is intentionally allowed to grow beyond 255 values and must
// stay distinct from the unrelated 8-bit browser-canvas Key enum. clang-tidy's
// performance-enum-size would prefer uint8 for today's value count; the wider,
// stable base type is a deliberate ABI/headroom choice, so the check is
// suppressed here with that rationale.
// NOLINTNEXTLINE(performance-enum-size)
enum class Key : uint16
{
    Unknown = 0,

    // Letters (US-position labels).
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

    // Top-row digits.
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

    // Punctuation positions (US reference layout).
    Minus,
    Equal,
    BracketLeft,
    BracketRight,
    Backslash,
    Semicolon,
    Quote,
    Backquote,
    Comma,
    Period,
    Slash,

    // Whitespace / control.
    Space,
    Enter,
    Tab,
    Backspace,
    Escape,

    // Editing and navigation.
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    ArrowLeft,
    ArrowRight,
    ArrowUp,
    ArrowDown,

    CapsLock,
    PrintScreen,
    ScrollLock,
    Pause,

    // Function row F1-F24.
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,

    // Keypad (kept distinct from the top-row digits and main operators).
    NumLock,
    KeypadDivide,
    KeypadMultiply,
    KeypadSubtract,
    KeypadAdd,
    KeypadEnter,
    KeypadDecimal,
    Keypad0,
    Keypad1,
    Keypad2,
    Keypad3,
    Keypad4,
    Keypad5,
    Keypad6,
    Keypad7,
    Keypad8,
    Keypad9,

    // Distinct left/right modifiers (K01).
    ShiftLeft,
    ShiftRight,
    ControlLeft,
    ControlRight,
    AltLeft,
    AltRight,
    SuperLeft,
    SuperRight,

    Menu,

    // One-past-the-end sentinel. Dense key storage is sized to this.
    Count,
};

// Number of dense key slots, including the reserved Unknown slot at index 0.
inline constexpr usize KEY_COUNT = static_cast<usize>(Key::Count);

// True when `key` is a real, in-range key that may index key storage. Unknown
// is deliberately NOT valid for indexing gameplay state: it is a sink value.
// This rejects fabricated enum values above Count as well as Unknown.
[[nodiscard]] inline constexpr bool IsValidKey(Key key) noexcept
{
    const uint16 raw = static_cast<uint16>(key);
    return raw > static_cast<uint16>(Key::Unknown) && raw < static_cast<uint16>(Key::Count);
}

// Dense index for a validated key. The caller MUST have checked IsValidKey.
[[nodiscard]] inline constexpr usize KeyIndex(Key key) noexcept
{
    return static_cast<usize>(key);
}

// Stable physical debug label (e.g. "KeyW", "ShiftLeft"). Never a layout- or
// locale-dependent character. Returns "Unknown" for Unknown or any value that
// is not a real key. Intended for diagnostics/trace, not gameplay.
[[nodiscard]] const char* DebugLabel(Key key) noexcept;
} // namespace ludus::input
