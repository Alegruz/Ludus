---
description: Measure monotonic intervals and use checked stopwatch, deadline and frame sampling values.
---

# Use high-resolution time

Use `Ludus::FoundationTime` for CPU elapsed-time measurements, real-time timeouts,
and explicit frame samples. The module supplies allocation-free, exception-free
values and `noexcept` operations. Each timer belongs to its caller; there is no
registration, callback service or initialization step.

## Add time to a game

In an SDK-consuming CMake project, link the target to your existing executable:

```cmake
find_package(Ludus CONFIG REQUIRED)
target_link_libraries(your_game PRIVATE Ludus::FoundationTime)
```

Replace `your_game` with your application's existing target. Use the project's
selected SDK and C++23 setup; see [project setup](project-setup.md). Include
`time.hpp` when naming timestamps/arithmetic and `timers.hpp` when naming timers.
These headers are explicit opt-ins, outside Base's `core.h`.

| Your task | Use | Ownership and units |
| --- | --- | --- |
| Measure CPU work | `Now()`, `TryElapsed`, `ToSeconds` | Same-domain `Timestamp` readings; integer `Duration` nanoseconds |
| Accumulate active time across pauses | `Stopwatch` | Owner-local state and explicitly supplied timestamps |
| Poll a real-time timeout | `Deadline` | Owner-local armed flag; checked timestamp plus duration |
| Sample a presentation frame | `FrameClock` | Persistent application-owned baseline and chosen maximum delta |
| Gameplay cooldown or authoritative schedule | Game-owned simulation tick deadlines | Advances with successful simulation ticks, including pause/single-step policy |

`Timestamp` and `Duration` are distinct types. Zero is valid in both. Nanoseconds
are the storage unit, not a physical resolution or wakeup guarantee. Supported
native timing uses a steady clock; the browser path uses the pinned Emscripten
clock, whose precision can be reduced by browser policy. Logging and profiling
share this source while preserving their existing timestamp units.

## Subtract before converting

Capture `timing::Now()` before and after the work being measured. Subtract those
integer readings with `TryElapsed`, then convert the resulting interval. Avoid
converting two large absolute timestamps to floating-point seconds before
subtracting: a short interval can disappear at long uptime.

This complete helper returns the reason for failure and preserves `seconds` on
failure:

```cpp
#include <ludus/foundation/base/types.h>

#include <ludus/foundation/time/time.hpp>

namespace timing = ludus::foundation::time;

timing::TimeStatus
TryElapsedSeconds(timing::Timestamp begin, timing::Timestamp end, ludus::foundation::float64& seconds) noexcept
{
    timing::Duration interval;
    const auto status = timing::TryElapsed(begin, end, interval);
    if (status != timing::TimeStatus::Ok)
    {
        return status;
    }
    seconds = timing::ToSeconds(interval);
    return timing::TimeStatus::Ok;
}
```

## Pause a stopwatch

`Start` begins a new interval and discards earlier history. `Pause` accumulates
active time; `Resume` excludes the paused gap. `Read` observes without advancing
state, and `Reset` stops the watch. Pausing a gameplay watch does not pause a UI
watch or stop the underlying clock.

These synthetic readings need no sleep: two active seconds, a 90-second pause,
and three more active seconds produce five seconds of accumulated time.

```cpp
#include <ludus/foundation/time/time.hpp>
#include <ludus/foundation/time/timers.hpp>

namespace timing = ludus::foundation::time;

bool ExamplePausedStopwatch() noexcept
{
    timing::Stopwatch watch;
    watch.Start(timing::Timestamp{1'000'000'000});
    if (watch.Pause(timing::Timestamp{3'000'000'000}) != timing::TimeStatus::Ok ||
        watch.Resume(timing::Timestamp{93'000'000'000}) != timing::TimeStatus::Ok)
    {
        return false;
    }
    timing::Duration elapsed;
    return watch.Read(timing::Timestamp{96'000'000'000}, elapsed) == timing::TimeStatus::Ok &&
           elapsed.Nanoseconds == 5'000'000'000;
}
```

In a live update, supply a sample from `Now()` instead. Share one sample among
related timer consumers. A paused read returns frozen time; a running read checks
against the latest transition, not the previous observational read. Callers
remain responsible for a monotonic sequence from one clock domain.

## Arm and poll a timeout

`Deadline` executes no work itself. The caller polls it and owns the operation,
cancellation and delivery policy. Equality is due; expiration keeps it armed.
Explicitly cancel or rearm after handling the timeout if it should fire once.
Zero delay is immediately due, and failed rearming preserves the old deadline.

```cpp
#include <ludus/foundation/time/time.hpp>
#include <ludus/foundation/time/timers.hpp>

namespace timing = ludus::foundation::time;

bool ExampleDeadline() noexcept
{
    timing::Deadline timeout;
    if (timeout.Arm(timing::Timestamp{1'000'000'000}, timing::Duration{500'000'000}) != timing::TimeStatus::Ok)
    {
        return false;
    }
    if (timeout.IsExpired(timing::Timestamp{1'499'999'999}) || !timeout.IsExpired(timing::Timestamp{1'500'000'000}))
    {
        return false;
    }
    timeout.Cancel();
    return !timeout.IsArmed() && !timeout.IsExpired(timing::Timestamp{2'000'000'000});
}
```

Use simulation ticks for a cooldown that should stop during game pause. A
real-time deadline keeps advancing when simulation stops.

## Keep frame stalls visible

Keep one `FrameClock` across frames and sample it once at the presentation
boundary. Pass the returned value to all consumers. The first sample establishes
a baseline with zero elapsed time. This example deliberately chooses a 250 ms
application cap; FoundationTime has no global frame cap.

```cpp
#include <ludus/foundation/time/time.hpp>
#include <ludus/foundation/time/timers.hpp>

namespace timing = ludus::foundation::time;

bool ExampleFrameStall() noexcept
{
    timing::FrameClock frames{timing::Duration{250'000'000}};
    if (!frames.Sample(timing::Timestamp{1'000'000'000}).IsBaseline)
    {
        return false;
    }
    const auto stall = frames.Sample(timing::Timestamp{2'000'000'000});
    if (stall.Status != timing::TimeStatus::Ok || stall.Elapsed.Nanoseconds != 250'000'000 ||
        stall.Discarded.Nanoseconds != 750'000'000)
    {
        return false;
    }
    const auto next = frames.Sample(timing::Timestamp{2'010'000'000});
    return next.Status == timing::TimeStatus::Ok && next.Elapsed.Nanoseconds == 10'000'000 &&
           next.Discarded.Nanoseconds == 0;
}
```

For a successful sample, raw elapsed time is accepted `Elapsed` plus `Discarded`.
The example reports the full one-second stall, accepts 250 ms and drops 750 ms;
the dropped time does not reappear as debt in the next frame. Preserve both
fields for diagnostics. Profiling scopes retain raw work measurements.

Call `Reset()` at startup, pause/resume, visibility suspension and session
replacement, according to application policy. Reset establishes a new baseline
on the next sample. A zero maximum delta deliberately freezes accepted time.
When adopting this helper into an existing simulation driver, preserve its
existing discard accounting and apply the frame clamp once.

## Handle failures and debug surprising readings

| Result or symptom | Meaning and recovery |
| --- | --- |
| `Overflow` | Addition cannot fit. `TryAdd` preserves its output; `Deadline::Arm` preserves its previous state. Reject the interval or choose a bounded policy; do not wrap or assume rearming succeeded. |
| `ClockRegression` | A supplied end/transition precedes its baseline. Checked arithmetic and stopwatch failures preserve outputs/state. `FrameClock` returns zero with this status and keeps its high-water mark, avoiding duplicate recovery time. Check sample ordering and ownership before resetting. |
| `InvalidState` | A stopwatch is stopped when read, or a pause/resume transition is invalid. Inspect `GetState()`; `Start` is a new interval, not a substitute for resume. |
| Equal readings | Valid zero elapsed time. Actual clock granularity can be coarser than the storage unit. |
| Unexpected catch-up after a load or hidden tab | Audit lifecycle resets, accepted/discarded time and the simulation debt policy. The clock backend alone does not decide whether suspension should advance gameplay. |

These values require caller synchronization if shared between threads.
Timestamps establish no happens-before relationship. Do not compare them with
UTC, simulation ticks, GPU counters, audio sample positions or another machine's
clock. The editor and runtime are separate processes: exchange runtime-reported
intervals/status through their protocol rather than subtracting editor samples
from host samples. Clock epochs are not persistent save/replay identities.

## What ships and what follows

| Area | Status |
| --- | --- |
| Shared native/browser clock, checked arithmetic and owner-local helpers | Implemented; explicit public SDK component |
| Logging/profiling clock producers | Use the shared source |
| Frame-loop migration and rational integer fixed-step replacement | Planned; existing drivers retain their policies |
| Bounded gameplay event queue and native frame pacing | Planned separate integrations |
| Editor Timing view and runtime timing telemetry | Proposed; no new timer controls or inspector ship with this slice |
| Time scaling, additional platform/worker domains, GPU/audio/network clock integration | Separate requirements and validation needed |

Existing Qt timers serve editor event-loop scheduling. A measurement API does
not replace that scheduling role or guarantee a precise sleep/wakeup.

## Sources and further reading

The [architecture and Gems review](../../architecture/high-resolution-time.md),
[ADR 0017](../../decisions/0017-foundation-time.md),
and [validation evidence](../../development/high-resolution-time-evidence.md)
record the adopted ideas, remaining scope and measurement limits. Thanks to
**Noel Llopis**, *The Clock: Keeping Your Finger on the Pulse of the Game*, Game
Programming Gems 4, §1.3, pp. 27–34, for independent pause state and shared frame
observations. The same review credits **Michael Harvey and Carl S. Marshall**,
*Scheduling Game Events*, Game Programming Gems 3, §1.1, pp. 5–14, and **John
Bolton**, *A Basic Scheduler*, Game Engine Gems 1, chapter 25, pp. 409–414, for
ideas informing the planned scheduling contract. Historical chapter code was
not copied; local reference PDFs are outside the public wiki.

See the [module guide](https://github.com/Alegruz/Ludus/blob/main/modules/foundation/time/README.md),
[time API](https://github.com/Alegruz/Ludus/blob/main/modules/foundation/time/include/ludus/foundation/time/time.hpp),
[timer API](https://github.com/Alegruz/Ludus/blob/main/modules/foundation/time/include/ludus/foundation/time/timers.hpp),
and [boundary tests](https://github.com/Alegruz/Ludus/blob/main/modules/foundation/time/tests/time_tests.cpp)
for exact implementation behavior.
