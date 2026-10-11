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
/// @brief Two optional normalized keyboard callbacks with borrowed user data.
///
/// UserData is passed back verbatim. Detach before destroying its owner.
/// Callbacks must immediately consume or copy their borrowed payloads.
struct KeyboardSink final
{
    /// Deliver one normalized transition for the attached gameplay window; null skips delivery.
    void (*OnRecord)(void* userData, const ludus::input::KeyboardRecord& record) noexcept = nullptr;
    /// Deliver a focus/lifecycle reset; FocusEntered carries held keys, cancellation an empty baseline.
    void (*OnReset)(void* userData,
                    ludus::input::ResetReason reason,
                    const ludus::input::FocusBaseline& baseline) noexcept = nullptr;
    /// Caller-owned callback context, borrowed until detachment or window destruction.
    void* UserData = nullptr;
};
} // namespace ludus::platform
