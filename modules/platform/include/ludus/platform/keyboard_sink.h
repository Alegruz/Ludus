#pragma once

// Normalized keyboard sink seam between a platform window and the input layer
// (K05, K06, K10). A backend callback NEVER invokes gameplay or renders; it only
// normalizes a native event into Input vocabulary and hands it to the attached
// sink. The sink is a plain noexcept function-pointer trio plus borrowed user
// data — no std::function, no allocation, no virtual dispatch.
//
// Ownership: the caller owns both the InputSystem (or whatever the user data
// points at) and the window. Detach the sink before destroying the input owner,
// and unregister surface routing before destroying the window. A sink must copy
// or immediately consume anything it is handed; it receives no pointer into a
// borrowed Wayland payload (the FocusBaseline is already a copied, normalized
// array).

#include <ludus/foundation/base/types.h>
#include <ludus/input/keyboard_event.h>

namespace ludus::platform
{
// Three callbacks, any of which may be null. UserData is passed back verbatim.
//
//  OnRecord  — one normalized key transition for the attached gameplay window.
//  OnReset   — a focus/lifecycle reset (leave/close/capability/seat loss) with a
//              baseline. FocusEntered carries the synchronized held set; the
//              cancellation reasons carry an empty, unfocused baseline.
//  (OnReset covers focus-enter too: reason == ResetReason::FocusEntered.)
struct KeyboardSink final
{
    void (*OnRecord)(void* userData, const ludus::input::KeyboardRecord& record) noexcept = nullptr;
    void (*OnReset)(void* userData,
                    ludus::input::ResetReason reason,
                    const ludus::input::FocusBaseline& baseline) noexcept = nullptr;
    void* UserData = nullptr;
};
} // namespace ludus::platform
