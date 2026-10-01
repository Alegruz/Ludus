#pragma once

// Normalized keyboard record and sink vocabulary (K01-K05, K10, K13).
//
// A KeyboardRecord is an OWNED, backend-independent value. Native callbacks (or
// headless fixtures / replay) produce these; the reducer consumes them. A record
// never borrows a pointer into a Wayland payload: enter key arrays are copied or
// normalized before the callback returns. The same record type is the single
// ingestion interface for Native, Synthetic (test), and Replay sources, so tests
// drive the exact production state machine.

#include <ludus/foundation/base/types.h>
#include <ludus/input/key.h>

namespace ludus::input
{
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;

// Where a record came from. Captured in the trace so a wrapped/partial replay is
// never mistaken for an exact capture.
enum class RecordSource : uint8
{
    Native,
    Synthetic,
    Replay,
};

// Physical transition carried by a key record.
enum class KeyTransition : uint8
{
    Down,
    Up,
};

// A single normalized key transition. `NativeTimeMs` is Wayland's uint32 ms with
// undefined origin and wrap — diagnostic metadata ONLY; the reducer never
// compares or sorts on it. `HasNativeTime` says whether it is meaningful.
struct KeyboardRecord final
{
    RecordSource Source = RecordSource::Synthetic;
    KeyTransition Transition = KeyTransition::Down;
    Key PhysicalKey = Key::Unknown;
    bool Repeat = false; // OS/compositor auto-repeat; never a gameplay press
    bool HasNativeTime = false;
    uint32 NativeTimeMs = 0;
};

// Reason a reset/baseline synchronization was requested. Distinguishes a focus
// enter (synchronize held set, no edges) from the various cancellation paths and
// the two explicit modes. ExplicitLive keeps the current physical baseline
// focused; ExplicitNeutral installs an empty, unfocused baseline.
enum class ResetReason : uint8
{
    None = 0,
    FocusLost,
    FocusEntered,
    WindowClosed,
    CapabilityLost,
    SeatRemoved,
    BackendDisconnected,
    Overflow,
    MapReplaced,
    ExplicitNeutral,
    ExplicitLive,
};

// Held-key baseline supplied on focus enter / explicit-live reset. The adapter
// copies Wayland's current held set into Keys (indexed by KeyIndex) before
// handing it over; the reducer installs it WITHOUT synthesizing press edges and
// marks every held key suppressed until a genuine Up-then-Down (K06).
struct FocusBaseline final
{
    bool Focused = false;
    bool Keys[KEY_COUNT] = {};
};
} // namespace ludus::input
