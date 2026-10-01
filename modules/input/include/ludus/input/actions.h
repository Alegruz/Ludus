#pragma once

// Small data-defined action map (K08, K09).
//
// Actions use caller-assigned integer IDs validated against a configured
// capacity. Only two kinds exist in this milestone: Button (ORs all bound,
// unsuppressed keys) and Axis1D (ORs negative and positive contributors
// separately and returns positive - negative as -1, 0, or +1). Bindings are
// plain records copied into owned bounded storage; there are no strings, hash
// lookups, delegates, or polymorphic per-key objects on the hot path. Defaults
// (Jump, MoveX, ...) belong to the application, not to this module.

#include <ludus/foundation/base/types.h>
#include <ludus/input/key.h>

namespace ludus::input
{
using ludus::foundation::float32;
using ludus::foundation::uint16;
using ludus::foundation::uint8;

// Stable, caller-assigned action identity. The valid domain is [0, capacity).
using ActionId = uint16;

enum class ActionKind : uint8
{
    Button,
    Axis1D,
};

// For an Axis1D binding, which side the key contributes to. Ignored for Button.
enum class AxisDirection : uint8
{
    Positive,
    Negative,
};

// One binding: a physical key contributes to an action. For a Button, Direction
// is ignored. For an Axis1D, Direction selects the contributor side.
struct Binding final
{
    ActionId Action = 0;
    ActionKind Kind = ActionKind::Button;
    AxisDirection Direction = AxisDirection::Positive;
    Key PhysicalKey = Key::Unknown;
};

// The current value and per-step edge flags for one action.
//
// Button: Value is 1 when active, else 0. Pressed is a false->true aggregate
// transition within the step; Released is true->false. Axis1D: Value is the
// final aggregate (-1/0/+1); Pressed/Released mean inactive<->active where
// active is nonzero. A +1 to -1 change without an intervening zero changes Value
// but is NOT a new Pressed. Cancelled marks an action that a reset/map-replace
// cancelled; it can coexist with a fresh Pressed later in the same step and does
// NOT fabricate a Released. Flags are OR-accumulated over the step.
struct ActionState final
{
    ActionKind Kind = ActionKind::Button;
    float32 Value = 0.0F;
    bool Pressed = false;
    bool Released = false;
    bool Cancelled = false;
};

// Status returned when installing a new binding map (K09).
enum class BindingStatus : uint8
{
    Ok,
    InvalidAction,    // an action id outside the configured domain
    InvalidKey,       // a binding referencing an invalid Key
    CapacityExceeded, // more bindings than the configured capacity
    ConflictingType,  // the same action id used as both Button and Axis1D
    DuplicateBinding, // identical (action, key, direction) appears twice
};

// Status for an action query (K08).
enum class ActionStatus : uint8
{
    Ok,
    InvalidAction, // id outside the configured domain
};
} // namespace ludus::input
