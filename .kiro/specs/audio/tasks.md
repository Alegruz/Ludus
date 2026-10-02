# Ludus audio implementation tasks

All implementation boxes start unchecked. Read [requirements](requirements.md),
[design](design.md) and [research](../../../docs/architecture/audio-research.md).
Execute A0-A7 in order, in reviewable batches; update the evidence ledger with
actual commands/results. Ordinary implementation decisions do not require a
new user approval. Missing hardware checks remain pending, not passed by mocks.

## A0 Audit and feasibility

Requirements: AU01-AU04, AU08, AU14-AU15. No prerequisite.

- [x] Read AGENTS.md, all applicable steering, ADRs 0003-0005/0007-0009,
  Containers/Memory docs, current native/web CMake/bootstrap/SDK and test tools.
- [x] Record actual revision and working-tree changes. Locate any concurrent
  audio work and preserve Input/Text/RHI/Platform/smoke/shader work and other specs.
- [x] Freeze API status/handle fields, capacity maxima, memory accounting and
  supported source formats. Create `docs/architecture/audio-decision-log.md`.
- [x] Include the Game Audio Programming 1-4 adaptations: preparation/voice
  states, resident groups, split listener, numeric silence causes, explicit PCM/
  phase contracts, user sliders and bounded application event/context policy.
  Preserve their bounded scope; do not require private PDFs or volume 5.
- [x] Lock miniaudio 0.11.23 source/hash/license/options and audit exact C APIs,
  allocation callbacks and low-level resampler call graph. Verify a native
  callback-only playback probe with stable owner lifetime.
- [ ] Probe pinned Emscripten 4.0.23 Wasm AudioWorklet plus shared queues/worker:
  real gesture, actual output layout/rate, isolation headers, no per-block JS
  allocation by Ludus and cancellation of async startup.
- [ ] Establish browser shutdown through actual context close and renderer/
  worker quiescence, including suspended/unstarted context and late callbacks.
  Verify stack/cookie lifetime in the executable probe, not only source reading.
- [x] Integrate deterministic cached/offline dependencies, C language flags and
  distinct native/web variants without overwriting existing presets.

Gate: common mixer architecture is feasible with pinned dependencies and no
exceptions. Keep a precise failed browser/resampler subgate pending if the
probe cannot establish its contract; proceed on independent native/offline
work. Do not install a different SDK or silently weaken callback/lifetime rules.
Dependency changes require a technical decision with evidence, not a new survey.

## A1 Control ownership and offline skeleton

Requirements: AU02-AU07, AU09, AU16. Prerequisite: A0 API freeze.

- [x] Add `modules/audio`, lightweight public headers and installed/export target.
- [x] Implement fallible fixed runtime allocation, session/generation handles,
  Disabled/Offline modes, prepared owned PCM seam and explicit service/shutdown.
- [x] Implement production SPSC batches, full validation/transaction rollback,
  128-frame boundaries, scheduled starts, and bounded application budgets.
- [x] Implement stop-generation and StopAll epoch mailboxes, durable terminal
  acknowledgments, stale-command rejection and safe asset retirement.
- [x] Implement explicit state transition causes, immutable published batches
  and transactional resident group MaxAdmitted charges with GroupCapacity.
- [x] Implement cached debug snapshot handoff with no racing double buffer;
  terminal ownership must remain independent of snapshot/trace loss. Queries
  overlay accepted reservations/acquired terminals onto stale renderer views.
- [x] Start an offline audio gym with named scenarios, owner-side filtering/
  numeric-tag correlation, admission/preparation errors and snapshot export.
- [x] Add production-path tests and tiny-capacity/counter-exhaustion seams.

Gate: these truth-table cases pass without any device, worker, Platform or window:

| Fixture | Required result |
| --- | --- |
| Invalid/cross-system/stale/fabricated handle | Specific failure, no out-of-range access or mutation |
| Invalid last record in a batch | No earlier plays applied, no leaked reservations or pins |
| Full command ring / oversized batch | QueueFull/InvalidArgument, unchanged prior accepted data |
| Batch straddles 64-command budget | Whole batch deferred, eventual progress, no partial update |
| Stop pending Play before command drain | Cancelled, no sample emitted, pin released after drain |
| Stop/StopAll with full queue | Both observed independently of normal queue |
| StopAll then new Play | Old epoch cancelled; new Play survives |
| Stale queued Update after reuse | Cannot modify the new generation |
| Dropped snapshots/completion notifications | Terminal acknowledgment still reclaims storage |
| Retire asset with pending/active play | New Play rejected, retained bytes valid until last terminal |
| Generation/session/epoch exhaustion | Explicit rejection/restart policy, no wrap alias |
| Start inside control quantum | Exact requested sample offset; synchronized batch layers |
| Late command / far-future start | LateStart or range rejection, no silent skip/rewind |
| Different RenderOffline request partition | Same boundary behavior and PCM under fixed command schedule |
| Disabled Play / zero-frame render | No phantom playback/clock advance; no output access |
| Group admission full / invalid last batch record | GroupCapacity or validation failure; every group/slot/pin charge rolled back |
| Group terminal with snapshots dropped | Charge released only through acquired terminal acknowledgment |
| Producer update flood / consumer paused mid-read | Published work stays immutable; no retraction, overwrite or consumer starvation |
| Cancel preparation then receive late success | Cannot reopen admission, resurrect playback or reuse owned storage early |
| Query newly accepted voice / stale pre-terminal snapshot | Pending is immediately known; acquired terminal cannot regress to cached active state |

## A2 Resident playback and bounded DSP

Requirements: AU04, AU08-AU11. Prerequisite: A1.

- [ ] Decode WAV/FLAC resident mono/stereo PCM at session rate with source byte,
  frame/channel/size/finite validation and fallible rollback.
- [ ] Implement per-instance cursors, EOF, half-open loops, frame conversion,
  scheduled offsets and sample ramps that persist across buffer partitions.
- [ ] Integrate preallocated audited resampling, Rate 0.5-2.0 and bounded input
  scratch. Derive ratios from rates, not buffer lengths; preserve fractional
  phase/history and guard neighbor reads. No callback init/free.
- [ ] Implement independent panning/attenuation positions, per-voice origin
  choice and listener validation; preserve true emitter positions and stereo beds.
- [ ] Implement logical/physical budgets, deterministic priority selection,
  per-group MaxSelected quotas, hysteresis, charged tails, virtual advance and
  kill policy. Keep selection fades out of audibility scores.
- [ ] Implement reversible Virtualizing and irreversible Stopping transitions;
  preserve variation/rate/cursor on reentry and expose silence/score causes.
- [ ] Validate application event descriptors and extend gym scenarios with
  group saturation, third-person listener and interrupted/reversed fades.
- [ ] Add allocation probes and DSP correctness/quality fixtures.

Gate tests: simultaneous independent voices sharing one clip; 1-frame/empty/
malformed/huge clips; mono/stereo channel placement; Rate 0.5/1/2 duration and
sine frequency; spectral alias rejection; ramp interruption; short and repeated
loop wraps; fractional virtual advance and reentry; source/resampler input/output
counts; scheduled starts; behind/coincident/out-of-range emitters; invalid
listener basis/NaN/Inf; 256 logical residents; exactly <=64 mixed voices including
tails; equal-priority hysteresis and stable ties; killing/stop/unload during fade.
Also test group quota saturation with free global slots; independent bus/group
assignments; overlapping group fades remaining <=64; selection reversal requiring
a group quota; Stop during Virtualizing never resurrecting; unchanged variation
on reentry; split listener with player-fixed distance and camera-driven pan;
camera-relative attenuation override; multiple simultaneous silence causes;
invalid descriptor fields and redundant owner-side transform updates.
Test 44100/48000 conversions in both directions, partial/one-frame tails and
partition parity; checked frame/sample capacities and distinct L/R impulses
through production planar/interleaved adapters. Repeated mute/unmute and mix-state
cycles must expire finite virtual sounds without stale playback bursts, leaked
pins or growing logical counts; virtualization must not free shared clip PCM.
No warm render/submit allocation or free including dependency calls.

## A3 Buses and mix control

Requirements: AU05, AU10-AU12, AU16. Prerequisite: A2.

- [ ] Implement static tree validation, child-before-parent accumulation and
  gains applied exactly once at each bus edge.
- [ ] Add user/base gain, base snapshot batch, bounded modifier instances,
  generation checks, fade removal and specified dB composition.
- [ ] Add per-channel input/post-gain meters with fixed windows, clipping/
  nonfinite counters and owner-side dBFS display with explicit silent values.
- [ ] Demonstrate dB sliders (initial 40 dB range), exact mute, remembered slider
  settings and category controls separate from gameplay mix.
- [ ] Demonstrate application-owned categories, dialogue attenuation and mute
  without hard-coded gameplay categories in the mixer.
- [ ] Demonstrate context policy above the mixer with resolved gain/priority/
  modifier commands and numeric policy tags; inspect contributions in the gym.
- [ ] Demonstrate a thin application adapter with owned-loop teardown, bounded
  event/owner cooldown and AlreadyActive suppression before admission, explicit
  clock/table-full reporting, and accepted-handle tracking independent of Mixed
  snapshots. Add a shared dialogue duck that releases only after the last durable
  terminal; reserve playback/required modifier together with failure rollback.

Gate: cyclic/invalid bus topology rejected before mutation; parent attenuation
not applied twice; user mute persists through mix changes; two identical active
modifiers have independent lifetimes; replacement/removal ramps from current
values; active/fading modifiers remain within eight slots; metered/clamped PCM
matches fixtures at each documented tap. Slider u=0/0.5/1 gives gain 0/0.1/1;
mute/unmute restores settings, interruptions ramp from current gain and invalid
inputs reject. Failed Play must not consume cooldown or create active/duck state;
same-owner duplicates include Pending/virtual voices; table saturation is explicit.
Overlapping dialogue ends in either order without premature unduck, even with
dropped snapshots, cancellation, errors or a terminal already reclaimed/reused
before the adapter polls. Policy entries reclaim after inactivity/cooldown and
owner generations prevent suppression of a newly created entity. Entity teardown stops owned loops
while detached one-shots can finish at their copied pose. No general graph,
macro interpreter, ECS integration or editor work in this milestone.

## A4 Preparation worker and music streaming

Requirements: AU03-AU07, AU08-AU10, AU13, AU16. Prerequisites: A1-A3.

- [ ] Add one fallible native decoder worker and its owner-produced job queue;
  native file and owned-memory byte readers have explicit lifetime/status.
- [ ] Implement four independent stream decoders/rings, prefill/readiness,
  deadline-first refill, incremental cold work units of <=4096 prepared frames,
  cancellation between units, fair deadline ties and EOF/error metadata.
- [ ] Implement worker-side exact loops, terminal acknowledgment and cancellation
  of late setup/refill. Browser encoded preload uses a distinct bounded cap.
- [ ] Implement production render consumption, hold-cursor starvation policy,
  fade/silence/recovery, protected physical slots and rate-1 restriction.
- [ ] Test real decoder-produced chunks and deterministic injected chunk schedules.

Gate: mono/stereo WAV/FLAC decode, partial final chunks, short prefill, malformed
source, failed allocation/read/seek/thread creation, stalled worker, full ring,
wraparound, repeated loops, two instances of the same source, stop during open/
refill/starvation, cancelled generation publication, no source close/free before
both acknowledgments, no EOF/underflow conflation, and cap/refill fairness.
Exercise a large cold preparation while established streams approach low water;
measure work-unit/read/seek/decode durations and minimum buffered runway. Verify
that cold decode yields between units and report uninterruptible I/O limitations.
Measure preparation-to-Ready separately from applied Play/output latency.
Use TSan on production queue/worker/renderer concurrency (separate build from
ASan). A single-thread offline fixture alone cannot validate stream synchronization.

## A5 Linux device integration

Requirements: AU04, AU09, AU14, AU16-AU17. Prerequisites: A0 native probe, A1-A4.

- [ ] Connect production renderer to stable private miniaudio device owner;
  report actual backend/rate/period/conversion and preserve backend error codes.
- [ ] Add explicit no-device failure/Disabled policy, suspension if supported,
  foreign-thread fault flags, quiesced shutdown and recovery via fresh session.
- [ ] Add native audio demo, generated/licensed fixtures and control-thread
  diagnostics/gym scenarios; audio continues through main/render-thread stalls.
- [ ] Validate audible output, device loss/restart, idle/zero voice behavior,
  cancellation/unload/shutdown and regressions against a real device.

Gate: offline tests remain device-independent; native hardware output is actually
heard/captured, documented and independent from miniaudio null backend success.
Missing device/compositor access is a pending gate, not evidence of playback.

## A6 Browser audio integration

Requirements: AU01-AU04, AU09, AU13, AU15-AU17. Prerequisites: A0 web lifecycle
probe, A1-A4. Native device hardware is not a dependency.

- [ ] Add separate audio web preset/build/package manifest and fixture server
  with isolation headers; preserve existing web presets and graphics behavior.
- [ ] Run the common mixer in Wasm AudioWorklet and common decoder in a private
  Wasm worker. Audit all shared object paths, atomics and allocation/logging seams.
- [ ] Implement async encoded fetch/preparation, source/rate parity, gesture
  resume, suspension, setup/processor failures and control-state reporting.
- [ ] Integrate proven close/quiescence teardown including late setup callbacks,
  suspended shutdown and repeated system creation; no buffers freed speculatively.
- [ ] Document actual memory/stack limits and all emitted worker/worklet/wasm
  files. Fail clearly without isolation or worklet support.
- [ ] Extend demo with genuine DOM gesture, pan/loop/music/mute/modifier controls
  plus split listener/group/reentry gym scenarios and filterable silence causes.
  No need for Text rendering to show HTML controls.

Gate: real browser AudioWorklet output, actual engine PCM signal captured for
analysis, gesture/suspend/resume, 44100/48000 contexts where available, missing
headers, failed fetch, main-thread stalls, worker stalls, processorerror and
shutdown/restart all exercised. Verify at least current Chromium and Firefox;
record versions/platforms, and label other browsers unverified. Offline/Node
wasm and mocked AudioContext are additional tests, never the live browser gate.

## A7 Evidence and final review

Requirements: AU01-AU17. Prerequisites: A1-A6; finish independent checks while
environment-dependent gates remain pending.

- [ ] Add complete resident replay fixtures and stream schedules/PCM capture;
  reject exact replay for incomplete traces and compare documented tolerances.
- [ ] Measure design section 12 workload matrix, warm allocation/frees, peak
  bytes, callback p50/p95/p99/max, stream starvation, deadlines, trace on/off,
  ten-minute stress and audible/quality limitations. Include reproduction tools.
- [ ] Verify native installed SDK and dedicated wasm export consumer, C/static
  dependency closure, every public header alone and include/build-time budgets.
- [ ] Run required pinned native and browser checks, TSan for concurrency and
  all relevant existing smoke/Input/Platform/RHI/Text/probe regressions.
- [ ] Review final diff against AGENTS.md; update README/API/build usage,
  decision log and `docs/development/audio-validation.md` with real evidence.
- [ ] Report exact supported capabilities, remaining gates and changed paths.
  Do not push, merge or deploy without a separate instruction.

Native required commands (verify checked-out CLI/preset support first):

```bash
./scripts/build linux-clang-debug
./scripts/test linux-clang-debug
./scripts/check linux-clang-development --all
./scripts/build linux-clang-asan-ubsan
./scripts/test linux-clang-asan-ubsan
./scripts/install-sdk linux-clang-development
./scripts/check-build-budget --profile
```

Also configure warnings-as-errors, header self-sufficiency/foundational checks,
audio/device OFF where supported, and a separate TSan build through the actual
project options. Build existing web presets plus the new dedicated audio preset,
run existing browser regression tooling and the real audio fixture server.
No blanket warning suppression, global exception enablement, raised header
budgets without measurements, or unrelated dependency migration.

## Evidence ledger

Fill this during implementation. A checked box is not evidence by itself.

| Milestone | Revision and changed paths | Commands and evidence | Result and pending gates |
| --- | --- | --- | --- |
| A0 | `6ed9836` on `feat/audio-system` (base `8972388`). Added `third_party/miniaudio/` (vendored 0.11.23, hash lock, C impl units, CMake), root `CMakeLists.txt` (C dep + audio module), `docs/architecture/audio-decision-log.md`. | miniaudio.h sha256 `7e4f3f13c8fe66df2080ac3dd12a89193e3c2463cb7f067c798abd7331cd8ee6` (4,099,492 bytes) verified on download and at configure; option macros verified against the pinned header; resampler/decoder call graph audited (see decision log). Toolchain provisioned: Clang/LLD/clang-format/clang-tidy 18.1.8, CMake 3.29.6, Ninja 1.11.1.3, Conan 2.8.1. miniaudio compiled as C and linked into `libludus_audio.a`. | Native callback-only playback probe and the browser AudioWorklet/shutdown-quiescence subgate remain **pending** (emsdk not installed; no device/browser in this sandbox). Common-mixer architecture is feasible with the pinned dependency and no exceptions; proceeding on native/offline work. |
| A1 | `6ed9836`. Added `modules/audio/` (public headers, control owner, SPSC ring, decode, tests). | `build linux-clang-debug` + `linux-clang-development` green; `ctest` 28/28 pass (adds `ludus_audio_tests` 26 cases/643 assertions and `ludus_audio_alloc_tests`). clang-format and clang-tidy (pinned 18) clean on all audio TUs; zero warm-path allocations verified. Truth-table rows exercised: invalid/stale/fabricated handles, invalid-last-record rollback, oversized batch, Stop-before-start, Stop/StopAll with full queue, StopAll-then-Play, disabled play, group admission rollback, dropped-snapshot terminal reclaim, retire-with-active-play, scheduled start offset, far-future rejection, render-partition parity, zero-frame no-op, slider u=0/0.5/1 -> 0/0.1/1. | Passed (device-independent). |
| A2 | Not started | None | Pending |
| A3 | Not started | None | Pending |
| A4 | Not started | None | Pending |
| A5 | Not started | None | Real Linux device gate pending |
| A6 | Not started | None | Real browser output and async teardown gate pending |
| A7 | Not started | None | Measurements, SDK, quality and validation pending |
