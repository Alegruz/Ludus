# ADR 0017: Shared monotonic time and explicit timer values

## Status

Accepted for the FoundationTime implementation slice, October 4, 2026.

## Context

Profiling and logging independently implement the same nanosecond clock. Games
also need inspectable elapsed-time, timeout and frame sampling state without
coupling their time policy to diagnostics or a callback scheduler.

## Decision

Add `Ludus::FoundationTime`, depending only on FoundationBase. Preserve the
existing native steady-clock and pinned Emscripten epoch/units, and delegate the
profiling/logging producers to it. Keep platform/chrono headers private.

Expose distinct unsigned integer `Timestamp` and `Duration` values, checked
arithmetic with explicit status and unchanged outputs on failure, and owner-local
stopwatch, deadline and frame sampling helpers driven by supplied timestamps.
Use independent inactive/lifecycle flags; zero is a valid sample. Frame clamping
reports discarded time. Preserve raw profiling readings.

Gameplay continues to use fixed tick deadlines; pause/replay, scheduling, pacing,
UTC, audio, GPU and network clocks have separate owners. Do not introduce direct
TSC reads, timer registrations, global clock mocks, initialization or a scheduler
without requirements and measurements. See the [architecture and Gems review](../architecture/high-resolution-time.md)
for contracts and staged extensions.

## Consequences

The SDK gains a small static FoundationTime dependency for logging/profiling and
an explicitly available public component. Existing clock call sites and trace
formats remain compatible. New code can test timing with synthetic samples and
handle reversed readings or overflow without exceptions. Nanosecond storage is
not a physical resolution or wakeup guarantee. Non-Linux native platforms and
browser worker/audio domains need their own validation before support expands.
