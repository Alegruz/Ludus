#pragma once

// Backend-independent keyboard reducer and simulation-step owner (K02, K03,
// K05-K09, K11, K12).
//
// InputSystem is a plain instance with explicit, caller-owned lifetime: no
// singleton, no worker thread, no mutex, no atomic queue, no observer list. All
// mutation and all queries happen on the main thread. Native callbacks (or
// headless fixtures / replay) call Ingest(); exactly one owner calls
// ConsumeStep() once per executed simulation step and then reads the published
// snapshot and actions.
//
// Hot methods are noexcept and allocate no heap memory: all storage is fixed and
// embedded. Fallible configuration (ReplaceBindings) reports failure explicitly
// and leaves the old state intact; it never throws.

#include <ludus/foundation/base/pointer.hpp> // ludus::foundation::core::UniquePtr
#include <ludus/foundation/base/types.h>
#include <ludus/input/actions.h>
#include <ludus/input/key.h>
#include <ludus/input/keyboard_event.h>

#include <span>

namespace ludus::input
{
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;
using ludus::foundation::usize;

// Fixed capacities (frozen in M0). Pending transitions queued between steps, the
// output step-event buffer, and the action/binding domains. Overflow of the
// pending ring is a bounded, observable loss event, never growth or a crash.
inline constexpr usize PENDING_CAPACITY = 256;
inline constexpr usize ACTION_CAPACITY = 64;
inline constexpr usize BINDING_CAPACITY = 256;

// Admission result for a single Ingest (K02, K07).
enum class AdmissionStatus : uint8
{
    Accepted,          // queued and/or changed live state
    Ignored,           // valid but redundant (repeat, duplicate down/up): no edge
    RejectedInvalid,   // unknown/fabricated key or out-of-focus gameplay record
    RecoveredWithLoss, // pending ring was full: batch discarded, suppression armed
};

// Step-advance result (K03). A repeated or decreasing id changes nothing.
enum class StepStatus : uint8
{
    Ok,
    InvalidStep,       // id not strictly greater than the last consumed id
    SequenceExhausted, // uint64 id space or the ingestion sequence is exhausted
};

// Immutable per-step keyboard view, published by ConsumeStep and stable until
// the next consumption/reset publication (K02, K05). Down is retained held
// state; Pressed/Released are OR-accumulated actual transitions in the step (not
// current & ~previous). A baseline-synchronized held key reports Down but no
// Pressed. Reset status is distinct from a fabricated Released.
struct KeyboardSnapshot final
{
    uint64 StepId = 0;
    uint32 MapVersion = 0;
    bool Focused = false;
    ResetReason LastReset = ResetReason::None;
    uint32 ResetEpoch = 0;
    bool Down[KEY_COUNT] = {};
    bool Pressed[KEY_COUNT] = {};
    bool Released[KEY_COUNT] = {};
    // True for keys installed by a baseline and not yet released+repressed; such
    // keys are Down but cannot activate actions (K06).
    bool Suppressed[KEY_COUNT] = {};
};

// One ordered record as it was consumed for the current step, including the
// reset boundary marker. Multiplicity is preserved: two Down/Up pairs appear as
// four entries (K02). Borrowed until the next ConsumeStep.
struct StepEvent final
{
    enum class Kind : uint8
    {
        Transition,
        ResetBoundary,
    };

    Kind Which = Kind::Transition;
    KeyTransition Transition = KeyTransition::Down;
    Key PhysicalKey = Key::Unknown;
    RecordSource Source = RecordSource::Synthetic;
    ResetReason Reset = ResetReason::None; // meaningful when Which == ResetBoundary
    uint64 Sequence = 0;
    uint64 StepId = 0;
};

// Owned, bounded binding configuration passed to ReplaceBindings. Copied into
// the system's storage on success; the caller's storage need not outlive it.
struct BindingMap final
{
    std::span<const Binding> Bindings;
};

// Diagnostic counters (always cheap, always available; K07, K13).
struct InputCounters final
{
    uint64 Accepted = 0;
    uint64 Ignored = 0;
    uint64 Rejected = 0;
    uint64 Repeats = 0;
    uint64 Unknown = 0;
    uint64 Duplicates = 0;
    uint64 Overflows = 0;
    uint64 DiscardedEvents = 0;
    uint64 Resets = 0;
};

class InputDebugTrace; // opt-in; defined in input_debug.h

class InputSystem final
{
public:
    InputSystem() noexcept;
    ~InputSystem() noexcept;

    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;
    InputSystem(InputSystem&&) = delete;
    InputSystem& operator=(InputSystem&&) = delete;

    // --- Ingestion (K02, K04, K07) --------------------------------------
    // Validate, update live state, and append an owned transition. Repeats and
    // duplicate down/up are Ignored without creating edges. Invalid keys and
    // records for an unfocused window are RejectedInvalid. Overflow returns
    // RecoveredWithLoss. noexcept, no allocation, no lock.
    AdmissionStatus Ingest(const KeyboardRecord& record) noexcept;

    // Current physical held state only (diagnostics, not simulation edges).
    [[nodiscard]] bool GetLiveDown(Key key) const noexcept;

    // --- Step ownership (K03) -------------------------------------------
    // stepId must be strictly increasing. Clears previous step edges, applies a
    // pending reset (if any) then all queued transitions in order, evaluates
    // actions, and publishes the snapshot and step-event view.
    StepStatus ConsumeStep(uint64 stepId) noexcept;

    [[nodiscard]] const KeyboardSnapshot& GetKeyboardSnapshot() const noexcept;

    // Ordered records consumed for the current step (borrowed until next step).
    [[nodiscard]] std::span<const StepEvent> GetStepEvents() const noexcept;

    // --- Actions (K08, K09) ---------------------------------------------
    [[nodiscard]] ActionStatus GetAction(ActionId id, ActionState& out) const noexcept;

    // Validate transactionally and commit only on success. On success a reset is
    // queued (MapReplaced) so old actions cancel and held keys are suppressed on
    // the next step. On failure the active map is untouched. Must not be called
    // from a callback or mid-consumption.
    BindingStatus ReplaceBindings(const BindingMap& map) noexcept;

    [[nodiscard]] uint32 GetMapVersion() const noexcept;

    // --- Reset / focus (K05, K06) ---------------------------------------
    // Queue a reset/baseline synchronization. FocusEntered/ExplicitLive install
    // the supplied held set as a suppressed baseline; the cancellation reasons
    // install an empty unfocused baseline. Coalesces with any pending reset into
    // the latest baseline while retaining reason accounting. Never needs queue
    // room.
    void RequestReset(ResetReason reason, const FocusBaseline& baseline) noexcept;

    // --- Diagnostics (K07, K13) -----------------------------------------
    [[nodiscard]] const InputCounters& GetCounters() const noexcept;

    // Attach/detach an opt-in bounded trace. Attaching does not transfer
    // ownership; the trace must outlive the attachment. Null detaches.
    void SetDebugTrace(InputDebugTrace* trace) noexcept;

    // True when construction allocated its fixed storage. If false, every
    // operation degrades to a safe no-op/neutral result; the caller should
    // treat construction as failed. Setup failure is reported explicitly here
    // rather than through an exception (engine code is -fno-exceptions).
    [[nodiscard]] bool IsValid() const noexcept
    {
        return mImpl.Get() != nullptr;
    }

private:
    struct Impl;
    // All reducer storage lives in Impl. It is allocated exactly once with a
    // non-throwing new at construction (the only allocation in the system) and
    // freed at destruction. No hot-path allocation ever occurs. The concrete
    // size is reported in the performance notes (M4).
    ludus::foundation::core::UniquePtr<Impl> mImpl;
};
} // namespace ludus::input
