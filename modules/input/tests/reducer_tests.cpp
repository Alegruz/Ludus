// Backend-independent reducer tests (M1). These drive the PRODUCTION state
// machine: the public InputSystem for ordinary cases, and the internal
// capacity-parameterized Reducer for tiny-capacity overflow / sequence
// exhaustion that the public frozen capacities make impractical to hit directly.

#include <ludus/input/keyboard.h>

#include "internal/reducer.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ludus::input;

namespace
{
KeyboardRecord down(Key key, RecordSource source = RecordSource::Synthetic) noexcept
{
    return KeyboardRecord{ .Source = source, .Transition = KeyTransition::Down, .PhysicalKey = key };
}
KeyboardRecord up(Key key, RecordSource source = RecordSource::Synthetic) noexcept
{
    return KeyboardRecord{ .Source = source, .Transition = KeyTransition::Up, .PhysicalKey = key };
}
KeyboardRecord repeat(Key key) noexcept
{
    return KeyboardRecord{ .Transition = KeyTransition::Down, .PhysicalKey = key, .Repeat = true };
}

// Focus a system so gameplay records are admitted. Empty baseline, focused.
void focus(InputSystem& system) noexcept
{
    FocusBaseline baseline;
    baseline.Focused = true;
    system.RequestReset(ResetReason::FocusEntered, baseline);
}

bool pressed(const InputSystem& system, Key key) noexcept
{
    return system.GetKeyboardSnapshot().Pressed[KeyIndex(key)];
}
bool released(const InputSystem& system, Key key) noexcept
{
    return system.GetKeyboardSnapshot().Released[KeyIndex(key)];
}
bool heldDown(const InputSystem& system, Key key) noexcept
{
    return system.GetKeyboardSnapshot().Down[KeyIndex(key)];
}
} // namespace

TEST_CASE("System constructs valid and starts neutral/unfocused", "[input][reducer]")
{
    InputSystem system;
    REQUIRE(system.IsValid());
    const KeyboardSnapshot& snapshot = system.GetKeyboardSnapshot();
    REQUIRE_FALSE(snapshot.Focused);
    REQUIRE(snapshot.StepId == 0);
    REQUIRE(system.GetStepEvents().empty());
}

TEST_CASE("Down, repeated Down, held idle step, Up: one press, held, one release", "[input][reducer]")
{
    InputSystem system;
    focus(system);
    REQUIRE(system.ConsumeStep(1) == StepStatus::Ok); // consume the focus baseline

    REQUIRE(system.Ingest(down(Key::KeyW)) == AdmissionStatus::Accepted);
    REQUIRE(system.Ingest(repeat(Key::KeyW)) == AdmissionStatus::Ignored); // auto-repeat
    REQUIRE(system.Ingest(down(Key::KeyW)) == AdmissionStatus::Ignored);   // duplicate down

    REQUIRE(system.ConsumeStep(2) == StepStatus::Ok);
    REQUIRE(pressed(system, Key::KeyW));
    REQUIRE(heldDown(system, Key::KeyW));
    REQUIRE_FALSE(released(system, Key::KeyW));

    // Idle step: held persists, edges clear.
    REQUIRE(system.ConsumeStep(3) == StepStatus::Ok);
    REQUIRE(heldDown(system, Key::KeyW));
    REQUIRE_FALSE(pressed(system, Key::KeyW));

    REQUIRE(system.Ingest(up(Key::KeyW)) == AdmissionStatus::Accepted);
    REQUIRE(system.Ingest(up(Key::KeyW)) == AdmissionStatus::Ignored); // redundant up
    REQUIRE(system.ConsumeStep(4) == StepStatus::Ok);
    REQUIRE(released(system, Key::KeyW));
    REQUIRE_FALSE(heldDown(system, Key::KeyW));
}

TEST_CASE("Down/Up in one step yields both edge flags and final up", "[input][reducer]")
{
    InputSystem system;
    focus(system);
    REQUIRE(system.ConsumeStep(1) == StepStatus::Ok);

    REQUIRE(system.Ingest(down(Key::Space)) == AdmissionStatus::Accepted);
    REQUIRE(system.Ingest(up(Key::Space)) == AdmissionStatus::Accepted);
    REQUIRE(system.ConsumeStep(2) == StepStatus::Ok);
    REQUIRE(pressed(system, Key::Space));
    REQUIRE(released(system, Key::Space));
    REQUIRE_FALSE(heldDown(system, Key::Space));
}

TEST_CASE("Two Down/Up pairs in one step: four ordered records, both edges", "[input][reducer]")
{
    InputSystem system;
    focus(system);
    REQUIRE(system.ConsumeStep(1) == StepStatus::Ok);

    REQUIRE(system.Ingest(down(Key::KeyJ)) == AdmissionStatus::Accepted);
    REQUIRE(system.Ingest(up(Key::KeyJ)) == AdmissionStatus::Accepted);
    REQUIRE(system.Ingest(down(Key::KeyJ)) == AdmissionStatus::Accepted);
    REQUIRE(system.Ingest(up(Key::KeyJ)) == AdmissionStatus::Accepted);
    REQUIRE(system.ConsumeStep(2) == StepStatus::Ok);

    REQUIRE(pressed(system, Key::KeyJ));
    REQUIRE(released(system, Key::KeyJ));
    REQUIRE_FALSE(heldDown(system, Key::KeyJ));

    // Multiplicity preserved in the ordered step-event view (4 transitions).
    auto events = system.GetStepEvents();
    int transitions = 0;
    for (const auto& event : events)
    {
        if (event.Which == StepEvent::Kind::Transition && event.PhysicalKey == Key::KeyJ)
        {
            ++transitions;
        }
    }
    REQUIRE(transitions == 4);
}

TEST_CASE("Several pumps with zero steps retain pending events", "[input][reducer]")
{
    InputSystem system;
    focus(system);
    REQUIRE(system.ConsumeStep(1) == StepStatus::Ok);

    REQUIRE(system.Ingest(down(Key::KeyA)) == AdmissionStatus::Accepted);
    REQUIRE(system.Ingest(down(Key::KeyD)) == AdmissionStatus::Accepted);
    // No ConsumeStep between "pumps"; live state tracks but snapshot does not.
    REQUIRE(system.GetLiveDown(Key::KeyA));
    REQUIRE(system.GetLiveDown(Key::KeyD));
    REQUIRE_FALSE(heldDown(system, Key::KeyA)); // snapshot not yet updated

    REQUIRE(system.ConsumeStep(2) == StepStatus::Ok);
    REQUIRE(heldDown(system, Key::KeyA));
    REQUIRE(heldDown(system, Key::KeyD));
    REQUIRE(pressed(system, Key::KeyA));
    REQUIRE(pressed(system, Key::KeyD));
}

TEST_CASE("Three ConsumeStep calls after one batch: edges only in first", "[input][reducer]")
{
    InputSystem system;
    focus(system);
    REQUIRE(system.ConsumeStep(1) == StepStatus::Ok);

    REQUIRE(system.Ingest(down(Key::KeyW)) == AdmissionStatus::Accepted);

    REQUIRE(system.ConsumeStep(10) == StepStatus::Ok);
    REQUIRE(pressed(system, Key::KeyW));
    REQUIRE(heldDown(system, Key::KeyW));

    REQUIRE(system.ConsumeStep(11) == StepStatus::Ok);
    REQUIRE_FALSE(pressed(system, Key::KeyW));
    REQUIRE(heldDown(system, Key::KeyW));

    REQUIRE(system.ConsumeStep(12) == StepStatus::Ok);
    REQUIRE_FALSE(pressed(system, Key::KeyW));
    REQUIRE(heldDown(system, Key::KeyW));
}

TEST_CASE("Querying the same snapshot repeatedly does not consume", "[input][reducer]")
{
    InputSystem system;
    focus(system);
    REQUIRE(system.ConsumeStep(1) == StepStatus::Ok);
    REQUIRE(system.Ingest(down(Key::KeyW)) == AdmissionStatus::Accepted);
    REQUIRE(system.ConsumeStep(2) == StepStatus::Ok);

    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(pressed(system, Key::KeyW));
        REQUIRE(heldDown(system, Key::KeyW));
    }
}

TEST_CASE("Duplicate or decreasing step IDs are InvalidStep and change nothing", "[input][reducer]")
{
    InputSystem system;
    focus(system);
    REQUIRE(system.ConsumeStep(5) == StepStatus::Ok);
    REQUIRE(system.Ingest(down(Key::KeyW)) == AdmissionStatus::Accepted);
    REQUIRE(system.ConsumeStep(6) == StepStatus::Ok);
    REQUIRE(pressed(system, Key::KeyW));

    // Repeated id: unchanged, pending (none) untouched; the Pressed edge stays.
    REQUIRE(system.ConsumeStep(6) == StepStatus::InvalidStep);
    REQUIRE(system.ConsumeStep(3) == StepStatus::InvalidStep);
    REQUIRE(pressed(system, Key::KeyW));
    REQUIRE(system.GetKeyboardSnapshot().StepId == 6);
}

TEST_CASE("Unknown and fabricated Key values are rejected without OOB access", "[input][reducer]")
{
    InputSystem system;
    focus(system);
    REQUIRE(system.ConsumeStep(1) == StepStatus::Ok);

    REQUIRE(system.Ingest(down(Key::Unknown)) == AdmissionStatus::RejectedInvalid);
    REQUIRE(system.Ingest(down(static_cast<Key>(50000))) == AdmissionStatus::RejectedInvalid);
    REQUIRE(system.ConsumeStep(2) == StepStatus::Ok);
    REQUIRE(system.GetCounters().Unknown == 2);
}

TEST_CASE("Focus enter with held W: raw Down, no press, action-eligibility suppressed", "[input][reducer]")
{
    InputSystem system;
    FocusBaseline baseline;
    baseline.Focused = true;
    baseline.Keys[KeyIndex(Key::KeyW)] = true;
    system.RequestReset(ResetReason::FocusEntered, baseline);
    REQUIRE(system.ConsumeStep(1) == StepStatus::Ok);

    REQUIRE(heldDown(system, Key::KeyW));      // physical state synchronized
    REQUIRE_FALSE(pressed(system, Key::KeyW)); // no synthesized press
    REQUIRE(system.GetKeyboardSnapshot().Suppressed[KeyIndex(Key::KeyW)]);

    // A duplicate Down (key already physically down) is ignored; suppression stays.
    REQUIRE(system.Ingest(down(Key::KeyW)) == AdmissionStatus::Ignored);
    // Release then repress clears suppression and yields a genuine press.
    REQUIRE(system.Ingest(up(Key::KeyW)) == AdmissionStatus::Accepted);
    REQUIRE(system.Ingest(down(Key::KeyW)) == AdmissionStatus::Accepted);
    REQUIRE(system.ConsumeStep(2) == StepStatus::Ok);
    REQUIRE_FALSE(system.GetKeyboardSnapshot().Suppressed[KeyIndex(Key::KeyW)]);
    REQUIRE(pressed(system, Key::KeyW));
    REQUIRE(heldDown(system, Key::KeyW));
}

TEST_CASE("Focus leave with queued tap/held key discards pending and shows reset", "[input][reducer]")
{
    InputSystem system;
    focus(system);
    REQUIRE(system.ConsumeStep(1) == StepStatus::Ok);
    REQUIRE(system.Ingest(down(Key::KeyW)) == AdmissionStatus::Accepted); // queued, not consumed

    FocusBaseline empty;
    empty.Focused = false;
    system.RequestReset(ResetReason::FocusLost, empty);

    REQUIRE(system.ConsumeStep(2) == StepStatus::Ok);
    REQUIRE_FALSE(system.GetKeyboardSnapshot().Focused);
    REQUIRE_FALSE(heldDown(system, Key::KeyW)); // pending down discarded
    REQUIRE_FALSE(pressed(system, Key::KeyW));
    REQUIRE(system.GetKeyboardSnapshot().LastReset == ResetReason::FocusLost);

    // Records while unfocused do not activate gameplay.
    REQUIRE(system.Ingest(down(Key::KeyW)) == AdmissionStatus::RejectedInvalid);
}

TEST_CASE("Reset then genuine fresh records before consumption keep order", "[input][reducer]")
{
    InputSystem system;
    focus(system);
    REQUIRE(system.ConsumeStep(1) == StepStatus::Ok);
    REQUIRE(system.Ingest(down(Key::KeyA)) == AdmissionStatus::Accepted); // will be discarded

    FocusBaseline baseline;
    baseline.Focused = true;
    system.RequestReset(ResetReason::FocusEntered, baseline);             // discards KeyA
    REQUIRE(system.Ingest(down(Key::KeyD)) == AdmissionStatus::Accepted); // queued behind baseline

    REQUIRE(system.ConsumeStep(2) == StepStatus::Ok);
    // Baseline applied first (no KeyA), then KeyD press.
    REQUIRE_FALSE(heldDown(system, Key::KeyA));
    REQUIRE(heldDown(system, Key::KeyD));
    REQUIRE(pressed(system, Key::KeyD));

    // The reset boundary precedes the KeyD transition in the ordered view.
    auto events = system.GetStepEvents();
    REQUIRE(events.size() >= 2);
    REQUIRE(events[0].Which == StepEvent::Kind::ResetBoundary);
}

TEST_CASE("Multiple resets before consumption keep the latest baseline and reason accounting", "[input][reducer]")
{
    InputSystem system;
    FocusBaseline first;
    first.Focused = true;
    first.Keys[KeyIndex(Key::KeyA)] = true;
    system.RequestReset(ResetReason::FocusEntered, first);

    FocusBaseline second;
    second.Focused = true;
    second.Keys[KeyIndex(Key::KeyD)] = true;
    system.RequestReset(ResetReason::FocusEntered, second); // coalesce; latest wins

    REQUIRE(system.ConsumeStep(1) == StepStatus::Ok);
    REQUIRE_FALSE(heldDown(system, Key::KeyA)); // first baseline superseded
    REQUIRE(heldDown(system, Key::KeyD));
    REQUIRE(system.GetCounters().Resets == 1); // coalesced into a single applied reset
}

// ---- Tiny-capacity cases via the internal reducer -----------------------

using TinyReducer = ludus::input::internal::Reducer<4, 8, 16, 0>;
using GuardReducer = ludus::input::internal::Reducer<256, 8, 16, 3>;

namespace
{
void focusTiny(TinyReducer& reducer) noexcept
{
    FocusBaseline baseline;
    baseline.Focused = true;
    reducer.requestReset(ResetReason::FocusEntered, baseline);
}
} // namespace

TEST_CASE("Tiny capacity overflow where last record is Up leaves no stuck state", "[input][reducer][overflow]")
{
    TinyReducer reducer;
    focusTiny(reducer);
    REQUIRE(reducer.consumeStep(1) == StepStatus::Ok);

    // Capacity is 4. Fill with alternating transitions ending in an Up so a
    // dropped release cannot leave an action/key stuck.
    REQUIRE(reducer.ingest(down(Key::KeyW)) == AdmissionStatus::Accepted);
    REQUIRE(reducer.ingest(up(Key::KeyW)) == AdmissionStatus::Accepted);
    REQUIRE(reducer.ingest(down(Key::KeyA)) == AdmissionStatus::Accepted);
    REQUIRE(reducer.ingest(up(Key::KeyA)) == AdmissionStatus::Accepted);
    // Fifth transition overflows: discards the incomplete batch, snapshots live
    // Down (all up here), suppresses, keeps focus.
    REQUIRE(reducer.ingest(down(Key::KeyD)) == AdmissionStatus::RecoveredWithLoss);

    REQUIRE(reducer.consumeStep(2) == StepStatus::Ok);
    // No key stuck down; live Down reflects the fifth record (KeyD down).
    REQUIRE_FALSE(reducer.snapshot().Down[KeyIndex(Key::KeyW)]);
    REQUIRE_FALSE(reducer.snapshot().Down[KeyIndex(Key::KeyA)]);
    REQUIRE(reducer.getLiveDown(Key::KeyD));
    REQUIRE(reducer.counters().Overflows == 1);
}

TEST_CASE("Overflow then release/repress recovers without focus cycle", "[input][reducer][overflow]")
{
    TinyReducer reducer;
    focusTiny(reducer);
    REQUIRE(reducer.consumeStep(1) == StepStatus::Ok);

    // Overflow with KeyW left physically down.
    REQUIRE(reducer.ingest(down(Key::KeyW)) == AdmissionStatus::Accepted);
    REQUIRE(reducer.ingest(down(Key::KeyA)) == AdmissionStatus::Accepted);
    REQUIRE(reducer.ingest(down(Key::KeyS)) == AdmissionStatus::Accepted);
    REQUIRE(reducer.ingest(down(Key::KeyD)) == AdmissionStatus::Accepted);
    REQUIRE(reducer.ingest(down(Key::KeyE)) == AdmissionStatus::RecoveredWithLoss);
    REQUIRE(reducer.consumeStep(2) == StepStatus::Ok);
    // Held keys suppressed (down but not fresh).
    REQUIRE(reducer.snapshot().Suppressed[KeyIndex(Key::KeyW)]);

    // Release/repress recovers without a focus cycle; still focused throughout.
    REQUIRE(reducer.snapshot().Focused);
    REQUIRE(reducer.ingest(up(Key::KeyW)) == AdmissionStatus::Accepted);
    REQUIRE(reducer.ingest(down(Key::KeyW)) == AdmissionStatus::Accepted);
    REQUIRE(reducer.consumeStep(3) == StepStatus::Ok);
    REQUIRE_FALSE(reducer.snapshot().Suppressed[KeyIndex(Key::KeyW)]);
    REQUIRE(reducer.snapshot().Pressed[KeyIndex(Key::KeyW)]);
}

TEST_CASE("Small counter near exhaustion yields explicit reset, no silent wrap", "[input][reducer][sequence]")
{
    GuardReducer reducer; // SeqGuard = 3
    FocusBaseline baseline;
    baseline.Focused = true;
    reducer.requestReset(ResetReason::FocusEntered, baseline);
    REQUIRE(reducer.consumeStep(1) == StepStatus::Ok);

    REQUIRE(reducer.ingest(down(Key::KeyA)) == AdmissionStatus::Accepted); // seq 1
    REQUIRE(reducer.ingest(up(Key::KeyA)) == AdmissionStatus::Accepted);   // seq 2
    REQUIRE(reducer.ingest(down(Key::KeyA)) == AdmissionStatus::Accepted); // seq 3 (== guard)
    // Next sequence is exhausted: explicit recovery, not a wrap.
    REQUIRE(reducer.ingest(up(Key::KeyA)) == AdmissionStatus::RecoveredWithLoss);
    REQUIRE(reducer.counters().Overflows == 1);
}

TEST_CASE("Step id exhaustion is reported explicitly", "[input][reducer][sequence]")
{
    InputSystem system;
    focus(system);
    const ludus::foundation::uint64 maxId = ~static_cast<ludus::foundation::uint64>(0);
    REQUIRE(system.ConsumeStep(maxId) == StepStatus::SequenceExhausted);
}
