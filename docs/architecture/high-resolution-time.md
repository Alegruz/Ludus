# High-resolution time

Status: FoundationTime implementation slice; simulation and scheduling extensions
below are proposed. Reviewed October 4, 2026. See [ADR 0017](../decisions/0017-foundation-time.md).

## Goals and independent baseline

Time should be precise at long uptime, inexpensive to read, and easy to inspect.
The implementation should have ordinary value ownership, explicit failure and
small public headers. The initial design, written before reviewing the catalog,
selected a shared monotonic source, distinct integer timestamps and durations,
checked arithmetic, owner-local elapsed-time state and explicit frame samples.
The article review below strengthened frame sampling and future scheduling
contracts without changing that baseline's backend or dependency direction.

Keep these domains separate:

| Domain | Representation and owner | Meaning |
| --- | --- | --- |
| CPU measurement and real-time timeout | FoundationTime `Timestamp`/`Duration` | A monotonic interval within this process/domain. |
| Simulation | Game-owned `uint64` completed tick and tick rate | Authoritative gameplay, pause, single step and replay. |
| Frame/presentation | Application-owned `FrameClock` and copied `FrameSample` | Once-per-frame accepted delta and discarded stall time. |
| UTC | Logging's private system-clock anchor | Human-readable labels; never deadline arithmetic. |
| Audio | Device/sample position | Audio scheduling follows the device timeline. |
| GPU | RHI query and explicit CPU/GPU calibration | CPU clock reads cannot substitute for GPU elapsed time. |
| Network | Protocol-owned synchronized/estimated clock | Never compare another machine's CPU timestamp directly. |

## One clock and small value types

`Ludus::FoundationTime` depends only on FoundationBase. Base does not depend on
Time. Logging links Time privately; Profiling uses it for its existing timestamp
entry point. Installed SDK metadata registers FoundationTime. `core.h` remains
unchanged: a consumer explicitly includes Time when naming its vocabulary.

```mermaid
flowchart LR
    Base[FoundationBase] --> Time[FoundationTime]
    Time --> Logging[FoundationLogging producer timestamps]
    Time --> Profiling[FoundationProfiling compatibility clock]
    Time --> Application[Application samples and timeout state]
    Application --> Simulation[Game-owned fixed ticks]
```

Arrows show dependency availability, not execution order. The public `time.hpp`
and `timers.hpp` contain only aliases, plain values, declarations and small
non-template accessors. Platform headers and `<chrono>` stay in `.cpp` files.
Everything is `noexcept`; failures return `TimeStatus`. There is no allocator,
registry, service initialization/shutdown, virtual dispatch, clock override,
calibration thread, engine lock or shared mutable timer state.

Native `NowTicks()` uses `std::chrono::steady_clock` converted to integer
nanoseconds, preserving the existing profiling/logging epoch. The
[C++ steady-clock contract](https://eel.is/c++draft/time.clock.steady) requires a
steady, non-decreasing source. The pinned Linux standard library delegates to
the OS monotonic clock. Retain this proven backend until a representative profile
shows that clock reads materially limit performance. Windows/macOS-specific
backends are future supported-platform work, not validated by this Linux PR.
Microsoft recommends [QPC rather than direct TSC reads](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)
and documents cross-thread tick uncertainty, frequency stability and integer
conversion hazards. Do not add raw TSC, frequency polling, core affinity or a
global atomic clamp to this architecture without measured evidence and a complete
migration/ordering contract. A clock timestamp is not a synchronization primitive;
use sequence numbers and actual happens-before edges for causal event order.

Browser `NowTicks()` preserves `emscripten_get_now()` from the pinned 4.0.23 SDK.
Its main-thread implementation uses `performance.now()`; integer conversion
cannot recover precision removed by browser privacy settings. Verified against
`src/lib/libcore.js` in the local SDK and the
[Emscripten API](https://emscripten.org/docs/api_reference/emscripten.h.html#c.emscripten_get_now).
Nanoseconds are the storage unit, not a claim of nanosecond resolution or read
latency. This slice targets the current main-thread browser engine. AudioWorklet,
separate worker origins, pthread synchronization and GPU/audio clocks require
separate contracts before expanding support.

`Timestamp` and `Duration` each hold one `uint64 Nanoseconds`, with no implicit
conversion between them. `TryElapsed(start, end, out)` subtracts integers before
`ToSeconds(interval)` converts the small result to `float64`. No API converts an
absolute timestamp to floating seconds. `TryAdd` and `TryElapsed` use
FoundationBase's opt-in checked integer helpers. `TryAdd` rejects overflow;
reversed samples return `ClockRegression`. Failed operations leave outputs unchanged;
zero is an ordinary timestamp/interval, never a sentinel. Unsigned intervals do
not model negative offsets. UTC anchoring uses its own signed private delta.

The representation can hold about 584 years of nanoseconds, but this is not a
backend-uptime guarantee: native signed durations may have a smaller range. Do
not persist clock epochs across sessions, infer UTC from them or treat wrap as a
normal recovery policy. There is no timestamp ABI change to existing trace or
breadcrumb records: `profiling::NowTicks()` remains a compatibility wrapper and
its denominator remains one.

## Elapsed-time state and failure behavior

Each helper is an allocation-free owner-local value; synchronization, if shared,
is the caller's responsibility. Supply the same explicit `Timestamp` to all
helpers in one update. Tests can use synthetic samples without a global mock,
OS sleep or a virtual time-source hierarchy.

| Helper | Contract |
| --- | --- |
| `Stopwatch` | `Start` begins a new interval; `Pause` accumulates active time; `Resume` excludes the paused gap; `Reset` stops it. Invalid transitions and reversed transition samples preserve state. `Read` preserves its output on failure. A paused read returns frozen time. |
| `Deadline` | Checked `Arm(now, delay)` preserves an existing deadline on overflow. Zero delay expires immediately, equality counts as due, expiration remains armed, and `Cancel` changes an explicit flag. It executes no callbacks. |
| `FrameClock` | First sample establishes a baseline and returns zero; accepted samples report `Elapsed` and `Discarded`; stalls advance the baseline even when clamped. Reset drops suspension/startup history. A backwards sample returns zero and retains the previous high-water mark. Equal readings are valid. |

Stopwatch reads are observational: they validate against the latest transition,
not the previous read. Callers must supply a monotonic sequence from one domain.
FrameClock tracks every accepted sample and therefore rejects recovery below its
high-water mark. Maximum accepted frame delta is supplied by the application;
zero deliberately freezes accepted time. Clamping is never applied to profiling
or real-time deadlines. Long loads, debugger stops and hidden browser intervals
must be visible in raw diagnostics even if simulation discards them.

## Frame and simulation integration

Read one timestamp at the presentation callback boundary. Pass the resulting
sample through presentation and the simulation driver. Sample profiling scopes
independently, since their purpose is to measure actual work inside that frame.
Reset frame and simulation debt on startup, pause/resume, visibility suspension
and session replacement. Lifecycle signals, rather than the backend's treatment
of OS sleep, define whether a suspended game catches up. On Linux,
[CLOCK_MONOTONIC excludes suspended time](https://man7.org/linux/man-pages/man3/clock_gettime.3.html);
other clocks may include it. Do not promise uniform suspend behavior from the
measurement API.

The first slice leaves the existing [fixed-tick driver](frame-update.md) in place:
60 Hz by default, 250 ms clamp, four catch-up ticks and explicit discarded time.
It inherits the shared source through Profiling. Do not clamp twice or lose its
existing discard accounting when migrating it to FrameClock in a later PR.

A future integer fixed-step driver should retain a rational phase rather than
rounding `1e9 / rate` to a nanosecond tick length. Accumulate `elapsed_ns * rate`
in checked `uint64` phase units; one tick consumes exactly `1e9` phase units.
Carry the remainder, bound catch-up, and report discarded whole ticks and the
initial frame clamp separately. Validate the multiplication/addition bound at
configuration. At 60 Hz, 1 second must produce exactly 60 ticks regardless of
frame partitioning. Increment the completed tick only after a successful tick;
exhaustion stops the session. Derive interpolation alpha from the remainder.
Keep simulation time scaling explicit and rational, with an overflow policy and
replay metadata, if a game actually requires it. Do not build a timer hierarchy
or floating-point scale controls speculatively.

## Future scheduling and frame pacing

Most gameplay cooldowns remain checked tick deadlines on their owning component.
A future bounded typed queue follows [the established gameplay contract](frame-update.md#timers-and-decision-cadence):
owner-thread mutation; `(DueTick, Sequence)` ordering; snapshot due records at
BeginTick; validate target world/entity generation and cancellation at delivery;
minimum one-tick delay for scheduling during dispatch; no recursive queue drain.
Use generation-checked handles and bounded copied payloads. World replacement
clears the queue. Overflow and deadline/sequence exhaustion are explicit faults
for authoritative gameplay. Periodic deadlines advance from their previous due
time to avoid drift; specify skip/coalesce/bounded catch-up behavior when late.
The policy cannot silently depend on a measured CPU budget.

Start with a contiguous bounded scan at demonstrated small counts. Compare it
with an indexed min-heap only when queue-size and due-density measurements justify
it. Timing wheels require a documented granularity and horizon. No callback
registry, worker per timer, dynamic allocation on dispatch or thread sleep belongs
in FoundationTime. A real-time async timeout service would need a separate
ownership/cancellation/shutdown contract and would use CPU timestamps, not game
entity tick deadlines.

Frame pacing belongs to Platform/RHI/application policy. Prefer presentation
callbacks on browsers; never busy-wait there. Native pacing may use an absolute
monotonic target and an OS wait, rechecking after an early wake or interruption.
Advance targets from the prior target to avoid relative-sleep drift and reset
missed schedules after a bounded lateness policy. A measured optional short spin
must have a strict budget and power policy. High-resolution measurement does not
imply precise wakeups. Queue depth and GPU completion remain graphics policies.

## Gems review and design changes

Selection used the local `references/game-dev-gems-toc.md`. Chapters were read in
full: scanned GPG3/GPG4 pages were rendered and OCRed; GEG1 text was extracted.
The clock chapter's diagrams were checked visually. No CD-ROM implementation was
available or copied. Historical performance statements are not current benchmarks.

| Article | Author and pages | Adaptation to the independent baseline |
| --- | --- | --- |
| GPG4 1.3, The Clock: Keeping Your Finger on the Pulse of the Game | Noel Llopis; printed 27-34, PDF 44-51 | Strengthened the one-sample-per-frame contract, independent pause state, synthetic time injection, and explicit stall accounting. |
| GPG3 1.1, Scheduling Game Events | Michael Harvey and Carl S. Marshall; printed 5-14, PDF 7-16 | Made real/simulation domains, periodic phase and equal-deadline ordering explicit in the future queue contract. |
| GEG1 25, A Basic Scheduler | John Bolton; printed 409-414, PDF 437-442 | Reinforced owner-thread execution, separating due-set discovery from delivery and removal, and measuring scan versus ordered structures. |

Llopis separates a physical source from manipulable timers and stabilizes all
frame observations after FrameStep. Ludus adopts those semantics through supplied
value samples instead of a virtual source/registered timer object graph. Integer
intervals retain long-uptime precision. The chapter's averaging is useful for a
labeled presentation metric, but is excluded from simulation debt, deadlines and
raw tracing because it redistributes hitches. Its frame cap becomes explicit
accepted/discarded output. Vsync queue management remains an RHI concern.

Harvey/Marshall distinguish real and virtual time, specify scheduled phases and
identify events through registrations. Ludus preserves fixed gameplay ticks and
adds stable tie/cancellation contracts rather than arbitrary subframe simulation
callbacks. Subdividing a coarse clock does not increase its physical accuracy.
CPU-budget-driven adaptive gameplay cadence would undermine replay and is
excluded; optional cosmetic work may have a separately documented budget.

Bolton's serial scheduler separates discovering due work, execution and cleanup.
Ludus uses that ownership principle for a future bounded typed queue, with
explicit overflow and generation checks. Its arbitrary task ordering and reset
of a recurring period from delivery time are unsuitable for deterministic ties
and drift-free periodic phase. Container choice remains a measured decision.

Relevant earlier profiling reviews remain in
[the profiling literature review](profiling-literature-review.md): absolute
single-precision timestamps lose interval precision, and diagnostic smoothing
must stay separate from retained timing evidence.

## Verification and implementation scope

This PR implements Time, checked arithmetic, stopwatch, deadline and frame
sampling; migrates logging/profiling producers; and adds native unit, installed
SDK and pinned web semantic/header coverage. It does not implement the future
queue, pacing, rational fixed-step replacement, scaling, GPU or network clocks.

Boundary tests cover max-value overflow, output preservation, zero/equal/reversed
samples, one-nanosecond intervals at a large epoch, lifecycle failures, independent
pause state, deadline equality/cancellation and exact stall discard accounting.
A concurrent-read test records worker failures without calling Catch assertions
from workers. Native and web compatibility tests check the shared epoch/units.
The installed consumer exercises only public headers and exported symbols.

Use the pinned warning-clean builds, full unit/contract tests, ASan/UBSan,
format/tidy, browser semantics and installed-SDK verification. Measure release
clock-read cost against the prior direct steady-clock implementation and inspect
symbols for allocator/lock dependencies. These measurements describe this host;
they are not nanosecond-resolution or portable latency guarantees. See
[validation evidence](../development/high-resolution-time-evidence.md).
