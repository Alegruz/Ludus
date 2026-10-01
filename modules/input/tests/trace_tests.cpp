// Trace and replay-fixture tests (M4). Verify the opt-in bounded trace captures
// order/source/sequence/step/map-version and reset reasons, that trace ring wrap
// is reported separately from gameplay loss, and that a complete normalized
// fixture replays through the production Ingest/ConsumeStep path with identical
// semantic results on repeated runs. An incomplete (wrapped / overflowed) trace
// must not claim exact-replay completeness.

#include <ludus/input/input_debug.h>
#include <ludus/input/keyboard.h>

#include "internal/reducer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <span>

using namespace ludus::input;

namespace
{
KeyboardRecord down(Key key, RecordSource source = RecordSource::Replay) noexcept
{
    return KeyboardRecord{ .Source = source, .Transition = KeyTransition::Down, .PhysicalKey = key };
}
} // namespace

TEST_CASE("Trace is disabled by default and attaches on demand", "[input][trace]")
{
    InputSystem system;

    InputDebugTrace trace;
    REQUIRE(trace.View().empty());
    system.SetDebugTrace(&trace); // attach BEFORE the focus reset so its boundary is traced

    FocusBaseline baseline;
    baseline.Focused = true;
    system.RequestReset(ResetReason::FocusEntered, baseline);

    REQUIRE(system.Ingest(down(Key::KeyW)) == AdmissionStatus::Accepted);
    REQUIRE(system.ConsumeStep(2) == StepStatus::Ok);

    // The accepted transition and the focus reset boundary are both visible.
    auto view = trace.View();
    bool sawTransition = false;
    bool sawReset = false;
    for (const TraceEntry& entry : view)
    {
        if (entry.Kind == TraceKind::Transition && entry.PhysicalKey == Key::KeyW)
        {
            sawTransition = true;
            REQUIRE(entry.Source == RecordSource::Replay);
            REQUIRE(entry.StepId == 2); // assigned to the executing step
            REQUIRE(entry.Sequence >= 1);
        }
        if (entry.Kind == TraceKind::ResetBoundary)
        {
            sawReset = true;
        }
    }
    REQUIRE(sawTransition);
    REQUIRE(sawReset);
    system.SetDebugTrace(nullptr);
}

TEST_CASE("Ignored repeat/duplicate records are visible but marked distinct", "[input][trace]")
{
    InputSystem system;
    FocusBaseline baseline;
    baseline.Focused = true;
    system.RequestReset(ResetReason::FocusEntered, baseline);
    REQUIRE(system.ConsumeStep(1) == StepStatus::Ok);

    InputDebugTrace trace;
    system.SetDebugTrace(&trace);

    REQUIRE(system.Ingest(down(Key::Space)) == AdmissionStatus::Accepted);
    REQUIRE(system.Ingest(down(Key::Space)) == AdmissionStatus::Ignored); // duplicate down

    int ignored = 0;
    for (const TraceEntry& entry : trace.View())
    {
        if (entry.Kind == TraceKind::IgnoredTransition)
        {
            ++ignored;
        }
    }
    REQUIRE(ignored == 1);
}

TEST_CASE("Trace ring wrap is reported as truncation, separate from gameplay loss", "[input][trace]")
{
    InputDebugTrace trace;
    // Fill beyond capacity to force a wrap.
    for (usize i = 0; i < TRACE_CAPACITY + 10; ++i)
    {
        trace.Record(TraceEntry{ .Kind = TraceKind::Transition, .Sequence = i });
    }
    REQUIRE(trace.HasWrapped());
    REQUIRE(trace.GetTruncatedCount() == 10);
    REQUIRE(trace.GetTotalRecorded() == TRACE_CAPACITY + 10);
    REQUIRE(trace.View().size() == TRACE_CAPACITY);
    REQUIRE_FALSE(trace.IsCompleteForReplay()); // wrapped => incomplete

    // A gameplay Overflow entry marks replay-incompleteness independently.
    InputDebugTrace other;
    other.Record(TraceEntry{ .Kind = TraceKind::Overflow });
    REQUIRE_FALSE(other.HasWrapped());
    REQUIRE(other.GetTruncatedCount() == 0);    // no trace-ring loss
    REQUIRE_FALSE(other.IsCompleteForReplay()); // but gameplay overflow => incomplete
}

TEST_CASE("A complete fixture replays with identical semantics on repeated runs", "[input][trace][replay]")
{
    // Fixture: known initial focused baseline, a button + axis map, a batch of
    // records across several steps, and at least one reset mid-stream.
    const Binding bindings[] = {
        Binding{ .Action = 0, .Kind = ActionKind::Button, .PhysicalKey = Key::Space },
        Binding
        {
            .Action = 1,
            .Kind = ActionKind::Axis1D,
            .Direction = AxisDirection::Positive,
            .PhysicalKey = Key::KeyD,
        },
        Binding
        {
            .Action = 1,
            .Kind = ActionKind::Axis1D,
            .Direction = AxisDirection::Negative,
            .PhysicalKey = Key::KeyA,
        },
    };

    struct Expected
    {
        float32 jump;
        float32 moveX;
    };

    auto runFixture = [&](InputDebugTrace* trace) -> Expected {
        InputSystem system;
        system.SetDebugTrace(trace);
        FocusBaseline baseline;
        baseline.Focused = true;
        system.RequestReset(ResetReason::FocusEntered, baseline);
        REQUIRE(system.ReplaceBindings(BindingMap{ .Bindings = bindings }) == BindingStatus::Ok);
        REQUIRE(system.ConsumeStep(1) == StepStatus::Ok);

        // Step 2: press D (+1) and Space (jump).
        REQUIRE(system.Ingest(down(Key::KeyD)) == AdmissionStatus::Accepted);
        REQUIRE(system.Ingest(down(Key::Space)) == AdmissionStatus::Accepted);
        REQUIRE(system.ConsumeStep(2) == StepStatus::Ok);

        // A mid-stream explicit reset to neutral, then step 3.
        system.RequestReset(ResetReason::ExplicitNeutral, FocusBaseline{});
        REQUIRE(system.ConsumeStep(3) == StepStatus::Ok);

        // Re-focus and press A (-1) at step 4.
        FocusBaseline refocus;
        refocus.Focused = true;
        system.RequestReset(ResetReason::FocusEntered, refocus);
        REQUIRE(system.Ingest(down(Key::KeyA)) == AdmissionStatus::Accepted);
        REQUIRE(system.ConsumeStep(4) == StepStatus::Ok);

        ActionState jump;
        ActionState moveX;
        (void)system.GetAction(0, jump);
        (void)system.GetAction(1, moveX);
        return Expected{jump.Value, moveX.Value};
    };

    InputDebugTrace traceA;
    const Expected a = runFixture(&traceA);
    const Expected b = runFixture(nullptr); // trace off: same semantics
    InputDebugTrace traceC;
    const Expected c = runFixture(&traceC);

    REQUIRE(a.jump == b.jump);
    REQUIRE(a.moveX == b.moveX);
    REQUIRE(a.jump == c.jump);
    REQUIRE(a.moveX == c.moveX);
    REQUIRE(a.moveX == -1.0F); // ended holding A after refocus
    REQUIRE(a.jump == 0.0F);   // jump was cancelled by the neutral reset

    // The fixture fit the trace ring, so it is a complete, replayable capture.
    REQUIRE(traceA.IsCompleteForReplay());
    REQUIRE_FALSE(traceA.HasWrapped());

    // Two runs with tracing on produce identical trace lengths and ordering.
    REQUIRE(traceA.View().size() == traceC.View().size());
    auto va = traceA.View();
    auto vc = traceC.View();
    for (usize i = 0; i < va.size(); ++i)
    {
        REQUIRE(va[i].Kind == vc[i].Kind);
        REQUIRE(va[i].PhysicalKey == vc[i].PhysicalKey);
        REQUIRE(va[i].Transition == vc[i].Transition);
        REQUIRE(va[i].StepId == vc[i].StepId);
        REQUIRE(va[i].Reset == vc[i].Reset);
    }
}

TEST_CASE("Overflow marks the trace incomplete for exact replay", "[input][trace][replay]")
{
    // Tiny-capacity reducer so a few records overflow; attach a trace and verify
    // the Overflow entry flows through and marks incompleteness.
    using TinyReducer = ludus::input::internal::Reducer<4, 8, 16, 0>;
    TinyReducer reducer;
    InputDebugTrace trace;
    reducer.setTrace(&trace);

    FocusBaseline baseline;
    baseline.Focused = true;
    reducer.requestReset(ResetReason::FocusEntered, baseline);
    REQUIRE(reducer.consumeStep(1) == StepStatus::Ok);

    for (int i = 0; i < 6; ++i)
    {
        const Key key = static_cast<Key>(static_cast<ludus::foundation::uint16>(Key::KeyA) + i);
        (void)reducer.ingest(down(key));
    }
    // Among the records at least one overflow occurred (capacity 4).
    REQUIRE(reducer.counters().Overflows >= 1);
    bool sawOverflow = false;
    for (const TraceEntry& entry : trace.View())
    {
        if (entry.Kind == TraceKind::Overflow)
        {
            sawOverflow = true;
        }
    }
    REQUIRE(sawOverflow);
    REQUIRE_FALSE(trace.IsCompleteForReplay());
}
