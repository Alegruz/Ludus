#pragma once

// The backend-independent keyboard reducer, as a capacity-parameterized template
// so production code and tests share the EXACT same state machine. Production
// instantiates it with the frozen capacities (see keyboard.cpp); tests
// instantiate a tiny capacity to exercise overflow and sequence exhaustion.
//
// This is a private implementation header: it is never installed and never
// included by a public header. All state is fixed and embedded; no method
// allocates heap memory or takes a lock (K12).

#include <ludus/foundation/base/core.h>
#include <ludus/input/actions.h>
#include <ludus/input/input_debug.h>
#include <ludus/input/key.h>
#include <ludus/input/keyboard.h>
#include <ludus/input/keyboard_event.h>

namespace ludus::input::internal
{
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::usize;

// Internal, pre-validated transition queued for the next step.
struct PendingTransition final
{
    KeyTransition Transition = KeyTransition::Down;
    Key PhysicalKey = Key::Unknown;
    RecordSource Source = RecordSource::Synthetic;
    uint64 Sequence = 0;
    bool HasNativeTime = false;
    uint32 NativeTimeMs = 0;
};

// Coalesced reset mailbox, independent of the pending-queue capacity (K05-K07,
// K09). Holds the latest baseline plus retained reason accounting.
struct ResetMailbox final
{
    bool Pending = false;
    ResetReason Reason = ResetReason::None;
    bool Focused = false;
    bool Baseline[KEY_COUNT] = {};
    uint32 ReasonMask = 0; // OR of (1u << reason) for every coalesced reset
};

inline constexpr uint32 reasonBit(ResetReason reason) noexcept
{
    return 1U << static_cast<uint32>(reason);
}

// One action's cached aggregate across a step. Negative/positive contributor
// counts let an Axis1D OR each side independently and recompute in O(1) per
// transition (K08).
struct ActionAggregate final
{
    ActionKind Kind = ActionKind::Button;
    bool Present = false; // in the active map
    uint32 PositiveActive = 0;
    uint32 NegativeActive = 0;
    float32 Value = 0.0F;
    bool WasActive = false; // active at start of step (for edge detection)
    bool Pressed = false;
    bool Released = false;
    bool Cancelled = false;
};

template <usize PendingCap, usize ActionCap, usize BindingCap, usize SeqGuard = 0>
class Reducer final
{
public:
    Reducer() noexcept
    {
        reset();
    }

    // ---- Live/snapshot access ------------------------------------------
    [[nodiscard]] bool getLiveDown(Key key) const noexcept
    {
        return IsValidKey(key) && mLiveDown[KeyIndex(key)];
    }
    [[nodiscard]] const KeyboardSnapshot& snapshot() const noexcept
    {
        return mSnapshot;
    }
    [[nodiscard]] const StepEvent* stepEvents() const noexcept
    {
        return mStepEvents;
    }
    [[nodiscard]] usize stepEventCount() const noexcept
    {
        return mStepEventCount;
    }
    [[nodiscard]] const InputCounters& counters() const noexcept
    {
        return mCounters;
    }
    [[nodiscard]] uint32 mapVersion() const noexcept
    {
        return mMapVersion;
    }

    void setTrace(InputDebugTrace* trace) noexcept
    {
        mTrace = trace;
    }

    // ---- Ingestion (K02, K04, K07) -------------------------------------
    AdmissionStatus ingest(const KeyboardRecord& record) noexcept
    {
        // Reject unknown/fabricated keys with no state change.
        if (!IsValidKey(record.PhysicalKey))
        {
            ++mCounters.Unknown;
            ++mCounters.Rejected;
            return AdmissionStatus::RejectedInvalid;
        }

        // Gameplay records for an unfocused system are ignored until a focused
        // baseline arrives (K06). Resets/baselines come through requestReset.
        if (!mFocused)
        {
            ++mCounters.Rejected;
            return AdmissionStatus::RejectedInvalid;
        }

        const usize index = KeyIndex(record.PhysicalKey);

        // Compositor/OS auto-repeat never produces a gameplay edge (K04).
        if (record.Repeat)
        {
            ++mCounters.Repeats;
            ++mCounters.Ignored;
            traceIgnored(record);
            return AdmissionStatus::Ignored;
        }

        // Redundant transitions: duplicate down on an already-down key, or an up
        // on an already-up key. Live state is unchanged; no queue entry (K04).
        const bool down = record.Transition == KeyTransition::Down;
        if (down == mLiveDown[index])
        {
            ++mCounters.Duplicates;
            ++mCounters.Ignored;
            traceIgnored(record);
            return AdmissionStatus::Ignored;
        }

        // Update live state immediately — even if the queue cannot accept the
        // transition (K07: overflow must not corrupt live Down).
        mLiveDown[index] = down;

        // An Up for a suppressed key clears suppression without a gameplay
        // release (K05/K06). The reducer applies suppression at step time, but
        // live suppression is cleared here so a later Down is a genuine press.
        if (!down)
        {
            mLiveSuppressed[index] = false;
        }

        const uint64 sequence = nextSequence();
        if (sequence == kSequenceExhausted)
        {
            // Sequence space exhausted: force a conservative reset rather than
            // allowing ordering ambiguity (K12). Snapshot the live state.
            overflowRecover(ResetReason::Overflow);
            return AdmissionStatus::RecoveredWithLoss;
        }

        if (mPendingCount >= PendingCap)
        {
            // Bounded overflow (K07): discard the incomplete pending batch,
            // snapshot live Down into the reset mailbox, suppress held keys, and
            // keep focus. Live Down already reflects this record.
            overflowRecover(ResetReason::Overflow);
            return AdmissionStatus::RecoveredWithLoss;
        }

        const usize slot = (mPendingHead + mPendingCount) % PendingCap;
        mPending[slot] = PendingTransition
        {
            .Transition = record.Transition,
            .PhysicalKey = record.PhysicalKey,
            .Source = record.Source,
            .Sequence = sequence,
        };
        ++mPendingCount;
        ++mCounters.Accepted;
        // Accepted transitions are traced at step time (applyTransition) so the
        // entry carries its assigned step id. Carry the record's native-time and
        // source on the pending entry for that later trace.
        mPending[slot].HasNativeTime = record.HasNativeTime;
        mPending[slot].NativeTimeMs = record.NativeTimeMs;
        return AdmissionStatus::Accepted;
    }

    // ---- Reset / focus (K05, K06, K09) ---------------------------------
    void requestReset(ResetReason reason, const FocusBaseline& baseline) noexcept
    {
        // A reset immediately discards the old pending queue; new records queue
        // behind the baseline (K05). The mailbox coalesces into the latest
        // baseline while retaining reason accounting.
        discardPending();

        const bool focused = installsFocusedBaseline(reason) ? baseline.Focused : false;

        mMailbox.Pending = true;
        mMailbox.Reason = reason; // latest reason wins for the published value
        mMailbox.ReasonMask |= reasonBit(reason);
        mMailbox.Focused = focused;

        if (installsFocusedBaseline(reason))
        {
            for (usize i = 0; i < KEY_COUNT; ++i)
            {
                mMailbox.Baseline[i] = baseline.Keys[i];
            }
        }
        else
        {
            for (usize i = 0; i < KEY_COUNT; ++i)
            {
                mMailbox.Baseline[i] = false;
            }
        }

        // Live state follows the baseline immediately so GetLiveDown reflects the
        // synchronized physical set even before the next step.
        for (usize i = 0; i < KEY_COUNT; ++i)
        {
            mLiveDown[i] = mMailbox.Baseline[i];
            mLiveSuppressed[i] = mMailbox.Baseline[i]; // held keys suppressed
        }
        mFocused = focused;
    }

    // ---- Step ownership (K03) ------------------------------------------
    StepStatus consumeStep(uint64 stepId) noexcept
    {
        if (mHasConsumed && stepId <= mLastStepId)
        {
            return StepStatus::InvalidStep;
        }
        if (stepId == kStepIdExhausted)
        {
            return StepStatus::SequenceExhausted;
        }

        mLastStepId = stepId;
        mHasConsumed = true;

        // Clear previous-step edges on keys and actions.
        clearStepEdges();
        mStepEventCount = 0;

        // Apply a pending reset first, if any (K05).
        if (mMailbox.Pending)
        {
            applyReset(stepId);
        }

        // Reduce all queued transitions in order (K03): every pending event is
        // assigned to THIS step; none spill to later catch-up steps.
        while (mPendingCount > 0)
        {
            const PendingTransition& pending = mPending[mPendingHead];
            applyTransition(pending, stepId);
            mPendingHead = (mPendingHead + 1) % PendingCap;
            --mPendingCount;
        }

        publishSnapshot(stepId);
        return StepStatus::Ok;
    }

    // ---- Bindings (K08, K09) -------------------------------------------
    BindingStatus replaceBindings(std::span<const Binding> bindings) noexcept
    {
        if (bindings.size() > BindingCap)
        {
            return BindingStatus::CapacityExceeded;
        }

        // Validate transactionally into scratch; commit only on success.
        ActionKind kinds[ActionCap];
        bool present[ActionCap] = {};
        for (const Binding& binding : bindings)
        {
            if (binding.Action >= ActionCap)
            {
                return BindingStatus::InvalidAction;
            }
            if (!IsValidKey(binding.PhysicalKey))
            {
                return BindingStatus::InvalidKey;
            }
            const usize id = binding.Action;
            if (present[id] && kinds[id] != binding.Kind)
            {
                return BindingStatus::ConflictingType;
            }
            kinds[id] = binding.Kind;
            present[id] = true;
        }

        // Duplicate detection: identical (action, key, direction) twice.
        for (usize i = 0; i < bindings.size(); ++i)
        {
            for (usize j = i + 1; j < bindings.size(); ++j)
            {
                const Binding& a = bindings[i];
                const Binding& b = bindings[j];
                if (a.Action == b.Action && a.PhysicalKey == b.PhysicalKey &&
                    (a.Kind != ActionKind::Axis1D || a.Direction == b.Direction))
                {
                    return BindingStatus::DuplicateBinding;
                }
            }
        }

        // Commit: copy bindings and action kinds into owned storage.
        mBindingCount = 0;
        for (const Binding& binding : bindings)
        {
            mBindings[mBindingCount++] = binding;
        }
        for (usize id = 0; id < ActionCap; ++id)
        {
            mActions[id].Present = present[id];
            mActions[id].Kind = present[id] ? kinds[id] : ActionKind::Button;
        }

        ++mMapVersion;

        // Switching maps cancels old actions, clears pending activations, and
        // suppresses held keys until fresh presses (K09). Use the live physical
        // baseline so held keys stay physically down but suppressed.
        FocusBaseline baseline;
        baseline.Focused = mFocused;
        for (usize i = 0; i < KEY_COUNT; ++i)
        {
            baseline.Keys[i] = mLiveDown[i];
        }
        requestReset(ResetReason::MapReplaced, baseline);
        return BindingStatus::Ok;
    }

    // ---- Action query (K08) --------------------------------------------
    ActionStatus getAction(ActionId id, ActionState& out) const noexcept
    {
        if (id >= ActionCap)
        {
            return ActionStatus::InvalidAction;
        }
        const ActionAggregate& aggregate = mActions[id];
        out.Kind = aggregate.Kind;
        out.Value = aggregate.Value;
        out.Pressed = aggregate.Pressed;
        out.Released = aggregate.Released;
        out.Cancelled = aggregate.Cancelled;
        return ActionStatus::Ok;
    }

    void reset() noexcept
    {
        for (usize i = 0; i < KEY_COUNT; ++i)
        {
            mLiveDown[i] = false;
            mLiveSuppressed[i] = false;
        }
        mPendingHead = 0;
        mPendingCount = 0;
        mMailbox = ResetMailbox{};
        mFocused = false;
        mSequence = 0;
        mLastStepId = 0;
        mHasConsumed = false;
        mStepEventCount = 0;
        mBindingCount = 0;
        mMapVersion = 0;
        mCounters = InputCounters{};
        for (usize i = 0; i < ActionCap; ++i)
        {
            mActions[i] = ActionAggregate{};
        }
        mSnapshot = KeyboardSnapshot{};
        mResetEpoch = 0;
    }

private:
    static constexpr uint64 kSequenceExhausted = ~static_cast<uint64>(0);
    static constexpr uint64 kStepIdExhausted = ~static_cast<uint64>(0);

    // Small testable sequence guard: when SeqGuard != 0 the sequence saturates
    // at SeqGuard, so a near-exhaustion test need not count to 2^64 (K12).
    [[nodiscard]] uint64 nextSequence() noexcept
    {
        if constexpr (SeqGuard != 0)
        {
            if (mSequence >= SeqGuard)
            {
                return kSequenceExhausted;
            }
        }
        if (mSequence == kSequenceExhausted - 1)
        {
            return kSequenceExhausted;
        }
        return ++mSequence;
    }

    static constexpr bool installsFocusedBaseline(ResetReason reason) noexcept
    {
        return reason == ResetReason::FocusEntered || reason == ResetReason::ExplicitLive ||
               reason == ResetReason::MapReplaced || reason == ResetReason::Overflow;
    }

    void discardPending() noexcept
    {
        if (mPendingCount > 0)
        {
            mCounters.DiscardedEvents += mPendingCount;
        }
        mPendingHead = 0;
        mPendingCount = 0;
    }

    void overflowRecover(ResetReason reason) noexcept
    {
        ++mCounters.Overflows;
        traceOverflow();
        // Snapshot current live Down into the mailbox and suppress all held keys,
        // keeping focus. Live Down already includes the rejected record (K07).
        FocusBaseline baseline;
        baseline.Focused = mFocused;
        for (usize i = 0; i < KEY_COUNT; ++i)
        {
            baseline.Keys[i] = mLiveDown[i];
        }
        requestReset(reason, baseline);
    }

    void clearStepEdges() noexcept
    {
        for (usize i = 0; i < KEY_COUNT; ++i)
        {
            mSnapshot.Pressed[i] = false;
            mSnapshot.Released[i] = false;
        }
        for (usize i = 0; i < ActionCap; ++i)
        {
            mActions[i].Pressed = false;
            mActions[i].Released = false;
            mActions[i].Cancelled = false;
            mActions[i].WasActive = isActive(mActions[i]);
        }
    }

    void applyReset(uint64 stepId) noexcept
    {
        ++mResetEpoch;
        ++mCounters.Resets;
        mLastResetReason = mMailbox.Reason;

        // Cancel previously-active actions: Cancelled=true, no fabricated edges.
        for (usize i = 0; i < ActionCap; ++i)
        {
            ActionAggregate& aggregate = mActions[i];
            if (aggregate.WasActive)
            {
                aggregate.Cancelled = true;
            }
            // Rebuild aggregate active-contributor counts from the baseline, with
            // all baseline-held keys suppressed (they contribute nothing yet).
            aggregate.PositiveActive = 0;
            aggregate.NegativeActive = 0;
            aggregate.Value = 0.0F;
            aggregate.WasActive = false;
        }

        // Install the baseline into the simulation masks WITHOUT press edges.
        mFocused = mMailbox.Focused;
        for (usize i = 0; i < KEY_COUNT; ++i)
        {
            mSnapshot.Down[i] = mMailbox.Baseline[i];
            mSnapshot.Suppressed[i] = mMailbox.Baseline[i];
            mLiveSuppressed[i] = mMailbox.Baseline[i];
            // Live down is already the baseline (set in requestReset), keep it.
        }

        // Record the reset boundary in the step-event stream (K02, K13).
        appendStepEvent(StepEvent
        {
            .Which = StepEvent::Kind::ResetBoundary,
            .Reset = mMailbox.Reason,
            .StepId = stepId,
        });
        if (mTrace != nullptr)
        {
            mTrace->Record(TraceEntry
            {
                .Kind = TraceKind::ResetBoundary,
                .Reset = mMailbox.Reason,
                .MapVersion = mMapVersion,
                .StepId = stepId,
            });
        }

        mMailbox.Pending = false;
        mMailbox.Reason = ResetReason::None;
        mMailbox.ReasonMask = 0;
    }

    void applyTransition(const PendingTransition& pending, uint64 stepId) noexcept
    {
        const usize index = KeyIndex(pending.PhysicalKey);
        const bool down = pending.Transition == KeyTransition::Down;

        // Raw key masks: Pressed/Released are OR-accumulated ACTUAL transitions.
        mSnapshot.Down[index] = down;
        if (down)
        {
            mSnapshot.Pressed[index] = true;
            // A fresh Down on a suppressed key does not clear suppression here;
            // only an Up clears it (release-then-repress). But a Down that is not
            // suppressed can activate actions. Design: baseline-held keys require
            // Up THEN Down. So the first Down after a baseline (without a prior
            // Up) should not happen for a baseline key — the first event for such
            // a key must be an Up. If a Down arrives for a still-suppressed key we
            // leave suppression set and it contributes nothing to actions.
        }
        else
        {
            mSnapshot.Released[index] = true;
            // An Up clears suppression (release); a later Down is a genuine press.
            mSnapshot.Suppressed[index] = false;
            mLiveSuppressed[index] = false;
        }

        // Action evaluation uses the EFFECTIVE down state: a suppressed key does
        // not contribute (K06).
        const bool suppressed = mSnapshot.Suppressed[index];
        updateActionsForKey(pending.PhysicalKey, down && !suppressed);

        appendStepEvent(StepEvent
        {
            .Which = StepEvent::Kind::Transition,
            .Transition = pending.Transition,
            .PhysicalKey = pending.PhysicalKey,
            .Source = pending.Source,
            .Sequence = pending.Sequence,
            .StepId = stepId,
        });

        if (mTrace != nullptr)
        {
            mTrace->Record(TraceEntry
            {
                .Kind = TraceKind::Transition,
                .Source = pending.Source,
                .Transition = pending.Transition,
                .PhysicalKey = pending.PhysicalKey,
                .HasNativeTime = pending.HasNativeTime,
                .NativeTimeMs = pending.NativeTimeMs,
                .MapVersion = mMapVersion,
                .Sequence = pending.Sequence,
                .StepId = stepId,
            });
        }
    }

    // Recompute every action bound to `key` after the key's effective-down state
    // changed to `effectiveDown`, folding Pressed/Released edges (K08).
    void updateActionsForKey(Key key, bool effectiveDown) noexcept
    {
        for (usize b = 0; b < mBindingCount; ++b)
        {
            const Binding& binding = mBindings[b];
            if (binding.PhysicalKey != key)
            {
                continue;
            }
            if (binding.Action >= ActionCap)
            {
                continue;
            }
            ActionAggregate& aggregate = mActions[binding.Action];
            if (!aggregate.Present)
            {
                continue;
            }
            const bool wasActive = isActive(aggregate);

            if (binding.Kind == ActionKind::Button)
            {
                adjustCount(aggregate.PositiveActive, effectiveDown);
            }
            else // Axis1D
            {
                if (binding.Direction == AxisDirection::Positive)
                {
                    adjustCount(aggregate.PositiveActive, effectiveDown);
                }
                else
                {
                    adjustCount(aggregate.NegativeActive, effectiveDown);
                }
            }

            recomputeValue(aggregate);
            const bool nowActive = isActive(aggregate);
            if (!wasActive && nowActive)
            {
                aggregate.Pressed = true;
            }
            if (wasActive && !nowActive)
            {
                aggregate.Released = true;
            }
        }
    }

    static void adjustCount(uint32& count, bool add) noexcept
    {
        if (add)
        {
            ++count;
        }
        else if (count > 0)
        {
            --count;
        }
    }

    static void recomputeValue(ActionAggregate& aggregate) noexcept
    {
        if (aggregate.Kind == ActionKind::Button)
        {
            aggregate.Value = aggregate.PositiveActive > 0 ? 1.0F : 0.0F;
        }
        else
        {
            const float32 positive = aggregate.PositiveActive > 0 ? 1.0F : 0.0F;
            const float32 negative = aggregate.NegativeActive > 0 ? 1.0F : 0.0F;
            aggregate.Value = positive - negative;
        }
    }

    static bool isActive(const ActionAggregate& aggregate) noexcept
    {
        return aggregate.Value != 0.0F;
    }

    void appendStepEvent(const StepEvent& event) noexcept
    {
        if (mStepEventCount < kStepEventCap)
        {
            mStepEvents[mStepEventCount++] = event;
        }
    }

    void publishSnapshot(uint64 stepId) noexcept
    {
        mSnapshot.StepId = stepId;
        mSnapshot.MapVersion = mMapVersion;
        mSnapshot.Focused = mFocused;
        mSnapshot.LastReset = mLastResetReason;
        mSnapshot.ResetEpoch = mResetEpoch;
        // Down/Pressed/Released/Suppressed already maintained during the step.
    }

    void traceIgnored(const KeyboardRecord& record) noexcept
    {
        if (mTrace == nullptr)
        {
            return;
        }
        mTrace->Record(TraceEntry
        {
            .Kind = TraceKind::IgnoredTransition,
            .Source = record.Source,
            .Transition = record.Transition,
            .PhysicalKey = record.PhysicalKey,
            .HasNativeTime = record.HasNativeTime,
            .NativeTimeMs = record.NativeTimeMs,
            .MapVersion = mMapVersion,
        });
    }

    void traceOverflow() noexcept
    {
        if (mTrace == nullptr)
        {
            return;
        }
        mTrace->Record(TraceEntry
        {
            .Kind = TraceKind::Overflow,
            .MapVersion = mMapVersion,
        });
    }

    static constexpr usize kStepEventCap = PendingCap + 1; // batch + reset marker

    // --- Live (callback-visible) state ----------------------------------
    bool mLiveDown[KEY_COUNT];
    bool mLiveSuppressed[KEY_COUNT];
    bool mFocused;

    // --- Pending ring ---------------------------------------------------
    PendingTransition mPending[PendingCap];
    usize mPendingHead;
    usize mPendingCount;
    uint64 mSequence;

    // --- Reset mailbox --------------------------------------------------
    ResetMailbox mMailbox;
    uint32 mResetEpoch;
    ResetReason mLastResetReason = ResetReason::None;

    // --- Step publication ----------------------------------------------
    uint64 mLastStepId;
    bool mHasConsumed;
    KeyboardSnapshot mSnapshot;
    StepEvent mStepEvents[kStepEventCap];
    usize mStepEventCount;

    // --- Actions / bindings --------------------------------------------
    Binding mBindings[BindingCap];
    usize mBindingCount;
    ActionAggregate mActions[ActionCap];
    uint32 mMapVersion;

    // --- Diagnostics ----------------------------------------------------
    InputCounters mCounters;
    InputDebugTrace* mTrace = nullptr;
};
} // namespace ludus::input::internal
