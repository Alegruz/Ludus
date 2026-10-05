# FoundationTime

Allocation-free monotonic CPU time and owner-local elapsed-time helpers.
Link `Ludus::FoundationTime`; explicitly include `time.hpp` or `timers.hpp`.

See the [wiki usage guide](../../../docs/wiki/guides/time.md) for complete
examples, lifecycle/error handling and implemented versus planned integrations.

```cpp
#include <ludus/foundation/time/time.hpp>
#include <ludus/foundation/time/timers.hpp>

namespace timing = ludus::foundation::time;
timing::FrameClock frames{timing::Duration{250'000'000}}; // Application policy.
const auto sample = frames.Sample(timing::Now());
// First sample is a baseline. Pass this sample to all frame consumers.
const auto seconds = timing::ToSeconds(sample.Elapsed);
```

Call `frames.Reset()` at pause/resume, visibility suspension and session changes.
`FrameSample::Discarded` reports clamped stalls; rejected backward readings return
`ClockRegression` without lowering the baseline. Samples must share one domain.

`Stopwatch` supports Start/Pause/Resume/Read/Reset with explicit timestamps.
`Deadline` supports checked Arm, Cancel and IsExpired. Zero delay is immediately
due; expiration does not automatically cancel it. These values own no callback,
thread or registration. Use simulation tick deadlines for gameplay cooldowns.

`TryAdd` and `TryElapsed` return `TimeStatus` and preserve outputs on failure.
Subtract integer epochs before converting a duration with `ToSeconds`. The clock
stores nanoseconds; browser privacy and hardware determine actual precision.
See [architecture](../../../docs/architecture/high-resolution-time.md) for
ownership, scope, platform contracts and the article review.
