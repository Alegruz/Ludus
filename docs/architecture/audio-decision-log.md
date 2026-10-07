# Ludus audio decision log

Status: living document, started 2026-10-02 during A0 implementation of the
[audio spec](../../.kiro/specs/audio/requirements.md) (see
[requirements](../../.kiro/specs/audio/requirements.md),
[design](../../.kiro/specs/audio/design.md),
[tasks](../../.kiro/specs/audio/tasks.md),
[research](audio-research.md)).

This log records decisions actually made while implementing A0-A7, with the
evidence behind them. It never fabricates a measurement, hash or hardware
result. "Pending" means not yet run in this environment, not "assumed passing".

## macOS native Audio (2026-10-06)

Device mode now selects **CoreAudio only** through the locked miniaudio 0.11.23
low-level adapter. Linux retains PulseAudio/ALSA. Context/init/start failures
return `DeviceError`; the adapter never certifies playback through miniaudio's
null backend. The session requests float32 interleaved stereo at the configured
rate and forwards `DevicePeriodFrames` as a hint. Hardware conversion and the
actual callback size remain miniaudio's responsibility. Backend OS types remain
private, and teardown stops/uninitializes the device before freeing its renderer.

The existing bounded POSIX decode worker now runs on Linux and macOS: one worker
refills all stream instances, with cancellation and publication through the
existing atomics. Native shutdown joins it before releasing decoder/input storage.
AudioContent acquisition uses the same fallible pthread creation/join mechanism
on both platforms. `Begin` launches file acquisition/decoding, `Poll` installs a
completed sound/music revision on the control owner, and `Cancel` joins before
freeing pending work. File-backed music retains open content revisions and clones
an independent reader for each play; atomic source replacement and lease release
do not invalidate an already playing stream.

Initialize an offline session with the default `SystemConfig`, or set
`SystemMode = Mode::Device` for default-device playback. All public calls belong
to the sole control owner. Call `Service` to collect callback/worker results and
`BeginShutdown` on that owner to synchronously finish native teardown. Native
stream preparation also works in Offline mode. Run the pinned macOS setup/build
workflow from [the getting-started guide](../wiki/getting-started/install.md).

Regression coverage includes concurrent memory streams, file-backed music loop
refills beyond prefilled PCM, independent retained readers, failed replacement
acquisition, cancellation, lease release and shutdown. The installed public SDK
consumer exercises stream refill/EOF and AudioContent's native loading worker.
The native device test opens three sessions (44.1/48 kHz), observes both resident
and stream cursor progress through real callbacks, and shuts down with live work.
It produces brief low-level output. Set `LUDUS_TEST_AUDIO_DEVICE=1` to require a
physical output device; otherwise absence reports a skip after checking failure
cleanup and an explicit Offline retry. Callback progress is not an acoustic
listening or latency measurement. Device selection UI, device-loss recovery,
notarization/entitlements, callback performance/latency and long-run stress remain
separate validation/implementation work.

Thanks to **David Reid**, [miniaudio Programming Manual](https://miniaud.io/docs/manual/index.html),
sections 1.1 (low-level device lifecycle), 2.2 (macOS runtime framework linking)
and 17 (backends), for the contracts used by this adapter. The vendored version's
section 2.2 is the authoritative build contract here: default macOS framework
linking happens at runtime; no new framework dependency or codec version is added.
Thanks to **Apple**, [pthread_join(3)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/pthread_join.3.html),
DESCRIPTION, for join-before-reclamation semantics. Existing historical milestone
evidence below retains its original scope.

## A0 — audit and feasibility

### Baseline checkout

- Branch at session start: `main`, clean working tree.
- Baseline revision: `8972388` (`docs(audio): refine design from Game Audio
  Programming 2`). Implementation work happens on `feat/audio-system`.
- Preserved subsystems (not modified by audio): FoundationBase/Containers/
  Logging/Profiling, Input, Platform, RHI, Text, smoke app, web probes, shader
  probe, other specs. Audio is additive: a new `modules/audio/` plus deliberate
  root-CMake and preset additions.

### Toolchain provisioning (this environment)

The reference toolchain in `config/tool_versions.json` was provisioned here:

| Tool | Pinned requirement | Provisioned | Source |
| --- | --- | --- | --- |
| Clang/Clang++ | >= 18.0.0 | 18.1.8 | `dnf install clang18` (Amazon Linux 2023) |
| LLD | >= 18.0.0 | 18.1.8 | `dnf install lld18` |
| clang-format | >= 18.0.0 | 18.1.8 | `dnf install clang18-tools-extra` |
| clang-tidy | >= 18.0.0 | 18.1.8 | `dnf install clang18-tools-extra` |
| CMake | == 3.29.6 | 3.29.6 | project-managed venv (`scripts/bootstrap`) |
| Ninja | == 1.11.1.3 | 1.11.1.3 | project-managed venv |
| Conan | == 2.8.1 | 2.8.1 | project-managed venv |

Environmental quirk (recorded, not a project defect): this Amazon Linux
`clang-tidy-18` prints `LLVM (http://llvm.org/):` as the first line of
`--version`, with the version number on line 2. `scripts/python/engine.py`
reads only line 1 to parse the version, so its doctor/validator rejected an
otherwise-correct clang-tidy 18.1.8. Rather than edit the shared bootstrap
infrastructure, a thin pass-through wrapper at
`out/host-tools/bin/clang-tidy` normalizes only the bare `--version` first line
and forwards every real invocation to `/usr/bin/clang-tidy-18`. `out/` is
git-ignored, so this does not alter tracked project files.

Baseline before any audio code: `build linux-clang-debug` green (230/230
targets); `test linux-clang-debug` green (26/26 tests). Wayland is unavailable
in this sandbox, so Platform builds its headless window backend; Audio does not
depend on Platform, so this does not affect the audio gates.

### miniaudio dependency lock

- Version: **0.11.23**, released 2025-09-11 (`MA_VERSION_MAJOR/MINOR/REVISION
  = 0/11/23`), the inspected released tag. Not the floating master.
- Source file: `miniaudio.h` (single-header), fetched from
  `https://raw.githubusercontent.com/mackron/miniaudio/0.11.23/miniaudio.h`.
- Size: **4,099,492 bytes**.
- SHA-256: **`7e4f3f13c8fe66df2080ac3dd12a89193e3c2463cb7f067c798abd7331cd8ee6`**
  (computed from the downloaded bytes; reproduced identically on two downloads).
- License: miniaudio offers "public domain or MIT-0" (MIT No Attribution). Ludus
  uses the **MIT-0** option, consistent with the design. The license text is
  installed with the SDK.
- Native and web builds use these same locked bytes; no download at ordinary
  build/configure time. An absent source cache must produce an actionable
  offline diagnostic.

### miniaudio compile options (verified against the 0.11.23 header)

Compile the implementation once as a private C translation unit. Verified
option macros present in this exact source:

Common (native + web): keep WAV and FLAC; disable everything else we do not use.
- `MA_NO_MP3`, `MA_NO_VORBIS` — no extra codecs.
- `MA_NO_ENGINE`, `MA_NO_RESOURCE_MANAGER`, `MA_NO_NODE_GRAPH`,
  `MA_NO_GENERATION` — no high-level engine / resource manager / node graph /
  waveform+noise generation. Ludus owns voice management and mixing.
- `MA_NO_ENCODING` — decode only.

Web-only (decoder/resampler, no device I/O or native threading):
- `MA_NO_DEVICE_IO` (implies no backends), `MA_NO_THREADING`.

Native keeps `ma_decoder`, `ma_resampler`, and device I/O. Linux backend
(PulseAudio/ALSA) selection stays private to the adapter; no PipeWire claim
until a pinned backend is demonstrated. The exact backend-enable set is frozen
when the Linux device owner lands in A5.

### Resampler audit (bounded, allocation-free rendering)

Verified in the 0.11.23 header:
- `ma_resampler_config_init(format, channels, rateIn, rateOut, algorithm)` and
  `ma_resampler_init(config, allocationCallbacks, resampler)`.
- `ma_resample_algorithm_linear` with configurable `linear.lpfOrder` low-pass
  filter (0 disables; `MA_MAX_FILTER_ORDER` for strongest anti-aliasing on
  downsample).
- External-heap init path via the resampling backend vtable
  (`onGetHeapSize`/`onInit` with a caller `pHeap`), plus
  `ma_resampler_reset`, `ma_resampler_set_rate`,
  `ma_resampler_process_pcm_frames`,
  `ma_resampler_get_required_input_frame_count` and
  `ma_resampler_get_expected_output_frame_count` for bounded, phase-preserving,
  rate-derived conversion.
- `ma_allocation_callbacks` lets Ludus route any dependency allocation through
  owned fallible storage and assert zero allocation on warm paths.

Decision: preinitialize one resampler per resident physical voice slot outside
rendering; never init/uninit in Play/Stop. Derive the ratio from
source/session rates and playback Rate, never from buffer lengths. The
rate-1 direct-PCM fast path is deferred until A2 proves it preserves
cursor/phase/filter continuity across the resampler boundary; until then the
resampler stays consistently active.

### Browser lifecycle (pinned Emscripten 4.0.23)

`config/web_toolchain.json` pins Emscripten 4.0.23
(`emsdk_archive_sha256 = a91a4c1f...f2d`). The emsdk is **not installed** in
this sandbox, and installing/building a browser binary plus a real
AudioWorklet capture are environment-dependent. Per the A0 gate, the browser
AudioWorklet / shutdown-quiescence subgate is kept **pending**: the design's
required contract (async `AudioContext.close()`, worklet/worker quiescence,
late-startup cancellation, suspended-context shutdown without waiting for
another callback, stack/cookie lifetime proof) is specified in code and the
host-bridge seam, but is only certified by a real browser run, which this
environment cannot perform. No lifetime or real-time guarantee is weakened to
claim a green checkbox; independent native/offline work proceeds.

### Frozen API surface (A0)

- Status enum, handle layout (`Session`/`Slot`/`Generation` `uint32`, zero
  session/generation invalid), capacity maxima and memory accounting are frozen
  in `audio_types.h` per the design's default planning limits.
- Supported source formats: WAV and FLAC only; everything else rejected with a
  specific status.
- See the public headers under `modules/audio/include/ludus/audio/`.

## A2 — resident playback and bounded DSP

### Decode and resident PCM

- `PrepareClip` decodes WAV/FLAC via the pinned private `ma_decoder`
  (`ma_decoder_init_memory` + `ma_decoder_read_pcm_frames`), converting to
  float32 at the session rate, validating channel count (mono/stereo only),
  finiteness and the sample-value product against the resident PCM cap.
- The clip owns the produced PCM immutably; voices share it via pins. The
  `Impl` destructor frees all clip PCM and resampler heaps (verified leak-free
  under ASan). PrepareClip is cold; it never runs in rendering.

### Resampler (rate 0.5-2.0, phase/history preserved)

- One `ma_resampler` is preinitialized per physical voice slot and channel
  count (mono + stereo), using `ma_resampler_init_preallocated` with an
  external heap acquired once outside rendering (`ma_resampler_get_heap_size`).
  `Process`/`SetRate`/`RequiredInputFrames`/`Reset` are the only warm calls; none
  allocates. Verified zero warm-path allocation with the full DSP active.
- The conversion ratio is derived from rates, never buffer lengths: the
  resampler is configured `rateIn = sessionRate`, `rateOut = round(sessionRate /
  Rate)`, so it consumes ~Rate source frames per output frame. Fractional phase
  and filter history live inside the resampler and persist across partial spans;
  the kernel tracks the integer source cursor to resolve EOF and half-open loop
  seams, assembling wrap-aware source scratch from immutable PCM.
- Anti-alias low-pass filtering uses `linear.lpfOrder` (frozen initial value 4;
  `MA_MAX_FILTER_ORDER` is 8). The rate-1 direct-PCM fast path stays deferred;
  the resampler is active consistently so cursor/phase/filter continuity holds.
- Verified by tests: hard-L/hard-R pan routing, planar vs. interleaved L/R
  parity (identical content, channels not collapsed), Rate 0.5/1/2 duration
  ordering, looping past clip length, 44100<->48000 both directions (finite,
  audible), distance-attenuation falloff near/mid/far, listener-basis NaN and
  invalid-distance rejection.

### Selection, groups and virtualization

- At each 128-frame boundary a deterministic ordered candidate list is built by
  bounded insertion (priority desc, then estimated audibility desc, stable ties
  prefer incumbents then admission order). Selection respects each group's
  MaxSelected and the global steady-state budget (56); losers follow
  AdvanceWhenVirtual (-> Virtualizing -> Virtual) or KillWhenInaudible
  (-> Stopping -> Terminal). A Virtual/Virtualizing voice reverses into Mixed
  when reselected within quota; Stopping cannot reverse. The audibility estimate
  excludes the selection fade envelope (so a fading voice is not driven
  permanently virtual). Score contributions (voice gain, prepared peak, distance
  gain, bus-chain gain) are exposed separately.
- Physical voices are a pool sized to the mixed-voice budget; a logical voice
  binds a physical slot (and its preinitialized resamplers) only while mixing or
  fading, and reuses the authoritative cursor on devirtualization. Finite
  advancing virtual voices expire at nonloop EOF even while muted.

### Spatial (mono emitters, equal-power pan, finite attenuation)

- Meter coordinates; default pose +X right, +Y up, Forward = -Z. Distance
  attenuation `A = (1-t)^2` with `t = clamp((d-Min)/(Max-Min),0,1)`; A=1 within
  Min, A=0 beyond Max; negative Min or Max<=Min rejected. Pan is
  `dot(normalize(panDisplacement), Right)` with Right = normalize(cross(Forward,
  Up)); equal-power gains `sqrt((1-p)/2)`, `sqrt((1+p)/2)`. Separate panning and
  attenuation positions with a per-voice `AttenuationOrigin`; the true emitter
  position is retained. Stereo beds preserve L/R via scalar gain.

### Validation

- `build linux-clang-debug` 28/28 ctest green (adds 9 DSP cases). ASan+UBSan
  build green and leak-free across all 40 audio cases. clang-format and
  clang-tidy (pinned 18) clean. Zero warm submit/render allocation verified with
  the resampler and decode paths exercised.
- Listening/alias-rejection spectral quality gates and real device output remain
  pending (no audio device in this sandbox); the DSP is validated numerically
  and for finiteness/channel-routing/duration here.

## A3 — buses and mix control

### Static bus tree (gain applied exactly once)

- `buses.cpp` accumulates per-bus stereo input, then folds child-before-parent
  in descending bus index (validation guarantees a parent index is smaller than
  its children). Each bus applies its own `CurrentEffective` gain once at its
  edge and adds the post-gain signal into the parent's input accumulator, so an
  ancestor gain is never multiplied into a voice twice. Verified: Master 0.5 x
  SFX 0.5 yields 0.25 at the root (not 0.5 or 0.125).
- `EffectiveGain[b] = UserGain[b] * BaseGain[b] * 10^(clamp(sum(weight*dB),-96,0)/20)`,
  with `UserGain==0` forcing exactly 0. User mute persists through base-gain
  changes (verified). Per-bus input and post-gain peak/RMS meters use a fixed
  ~1024-frame window; pre-clamp overs are counted at the root and surfaced as
  `ClippedFrames` + `SystemSnapshot.PreClipFrames`. dBFS display is owner-side;
  no LUFS/true-peak claim.

### Attenuation modifiers

- `AcquireModifier`/`UpdateModifier`/`ReleaseModifier` manage up to 8
  generation-checked instances; overlapping owners get distinct slots and
  removing one never removes another. Values/weights validated (bus index in
  range, dB in [-96,0], weight in [0,1]).

### Application adapter (presentation/event policy)

- `audio_app_adapter.{h,cpp}`: a bounded fixed-capacity policy table keyed by
  (eventId, ownerTag) with cooldown and AlreadyActive suppression evaluated
  BEFORE admission; TableFull for a new key when the table is full. Only
  successful engine admission updates cooldown/active state — a failed admission
  (GroupCapacity/QueueFull/NotReady) consumes no cooldown and creates no
  activity or duck (verified). Owned loops are tracked and stopped on entity
  teardown; detached one-shots may finish. The shared dialogue duck acquires one
  modifier on the first accepted member and releases it only after the last
  member durably terminates, using handle validity (not a snapshot) so cleanup
  survives dropped diagnostics.

### Known v1 limitations (recorded, not defects)

- Bus and modifier gains are applied at 128-frame control boundaries, not
  sample-ramped. `ReleaseModifier` drops the modifier immediately rather than
  fading its weight to zero and awaiting a renderer-generation acknowledgment;
  this is observably correct under the single-threaded serialized owner, but
  the audible release is a boundary step, not a smooth fade. Sample-accurate bus
  ramps and acknowledged modifier fade-out are deferred.

## Audit (post-A3) — findings and fixes

A behavioral audit against AU01-AU17 / design sections 3-11 found and FIXED the
following concrete violations in the implemented A0-A3 code:

1. **Batch splitting across the 64-record boundary budget (AU05, section 4).**
   `ConsumeAndApplyBatch` drained individual records up to 64 with no
   batch-boundary tracking, so a batch straddling the budget was split (partial
   application). Fix: added `QueuedCommand.BatchLength` stamped on each batch's
   leading record; the consumer now defers a whole batch that would exceed the
   remaining budget. New test `batch_order_tests` asserts exactly 50 of 75
   records apply in one boundary (two whole 25-record batches), never 64.

2. **Mixed-batch command reordering (section 4 Play/Stop order).** The publish
   path emitted all Play records first, then all non-Play records, reordering a
   mixed batch such as `[Stop, Play]`. Fix: queued records are now built in
   original command order in a single ordered pass (reservation still happens
   first, but emission preserves order). New tests cover batch-local Stop/Update
   after a Play and invalid-local-reference rejection.

3. **Terminal reclamation not gated on the durable mailbox (section 4).**
   `AcknowledgeTerminals` reclaimed slots by reading `v.State == Terminal`
   directly instead of ACQUIRING `TerminalMailbox.TerminalGeneration`. Correct
   under the current single thread but a data race once the renderer runs on a
   device/worklet thread. Fix: reclamation now loads the terminal generation
   with acquire ordering and matches it to the live slot generation (pairs with
   the renderer's release in `TerminateVoice`).

4. **Virtual-advance fractional-cursor drift (section 7.2).** `AdvanceVirtualVoice`
   truncated `rate*frames` to an integer each span, dropping the remainder, so a
   non-integer-rate virtual voice drifted off the mixed timeline and expired
   late. Fix: the fractional remainder is now carried on the voice
   (`VoiceSlot.CursorFraction`) across spans. New test verifies a Rate-1.3
   virtual voice expires on time.

All four fixes build warning-clean and pass debug/ASan/UBSan/TSan. The audit
also confirmed (no change needed): SPSC release/acquire pairing and lock-free
`uint32` atomics; transactional validate-reserve-publish with full rollback and
tentative group charge committed only after publication; Stop/StopAll deliverable
with a full queue; Stop-before-start cancellation; generation/session/epoch
exhaustion rejection; <=64 physical mixed voices via the fixed pool; equal-power
pan and (1-t)^2 attenuation with split listener and per-voice origin; selection
score excludes the fade envelope; stereo beds preserve L/R (scalar gain, not
collapsed); planar/interleaved L/R parity; adapter cleanup via handle validity.

### Pending gates (unchanged by this audit — require unavailable hardware)

- Real Linux device playback, device loss/restart quiescence (A5).
- Real browser AudioWorklet/worker build, gesture resume, suspension/processor
  failure, async context close before reclamation (A6); emsdk not installed.
- Listening/spectral alias-rejection quality, p99 callback workload, 10-minute
  stress, installed-SDK consumer and build-time budget profiling (A7).
- Streaming worker / music streaming (A4) is not yet implemented.

## Reconciliation pass (post-A3) — spec gap closure

A reconciliation of the implemented A0-A3 code against the revised
requirements/design and the Game Audio Programming 1-4 follow-ups closed the
following gaps WITHOUT disturbing conforming work (groups, split listener,
transactional admission, SPSC ring, bus tree, dB sliders, adapter cooldown/duck
all preserved). Default group zero (256/56) and coincident listener positions
preserve prior behavior.

### Fixed in this pass

1. **Sub-span scheduled start (design section 5, AU09).** A voice whose exact
   StartFrame fell inside a 128-frame span previously began mixing at the span
   boundary (up to 127 frames early). The render loop now computes a per-voice
   start offset within the span: `MixAccum[0..offset)` stays zero and the kernel
   writes from the offset, so each voice emits zero before its start then
   samples. Test: a StartFrame=300 voice is exactly silent in [0,300) and
   audible after.

2. **Independent silence-cause flags completed and recomputed per boundary
   (design section 11).** Previously only BelowThreshold/GroupQuota/GlobalBudget/
   WaitingForFadeSlot were set, and flags leaked across boundaries. `SelectAndCharge`
   now resets every live voice's `Silence` each boundary and populates the base
   causes NotStarted (Pending/Scheduled), Stopping, DistanceZero (positional,
   fully attenuated) and UserMuted (any zero UserGain in the bus chain); the
   selection loop adds the not-selected causes without clearing the base ones (a
   selected voice on a user-muted bus still reports UserMuted). `TerminateVoice`
   clears transient causes so a terminal/stale slot reports its terminal reason,
   not stale flags. Tests cover NotStarted, UserMuted, GroupQuota and the
   per-boundary clear on unmute.

3. **Typed event descriptors + validation + variation selection (design
   section 9, AU16).** Added `EventDescriptor` (bounded variation list,
   bus/group/priority/gain/rate/distance/policy) plus
   `EventAdapter::ValidateDescriptor` returning the specific offending
   `DescriptorField` (empty/oversized variation list, unprepared variation
   handle with index, priority/gain/rate/distance), and `TriggerDescriptor`
   which selects one variation deterministically from a seed (recorded; never
   re-rolled on reentry since the renderer stores the clip slot at admission) and
   runs the policed admission path. An invalid descriptor emits an owner-side
   `PreparationFailed` event and admits nothing. Added `AudioSystem::IsClipReady`
   so the owner-side validator can check a variation handle without playback.

4. **Incremental cold-worker decode scheduler (design section 9; vol 1 ch.3 /
   vol 3 ch.9).** Added `internal/decode_scheduler.{hpp,cpp}`: the deadline-first
   incremental decode DISCIPLINE as a pure, bounded, deterministically-testable
   unit over an abstract `DecodeSource`. A cold whole-source prepare is split
   into units of at most `COLD_DECODE_UNIT_FRAMES` (4096) prepared output
   frames; between units the scheduler honours cancellation, rechecks active
   streams ordered by time-to-empty (least buffered first), breaks equal
   deadlines round-robin, and services the most-starved stream before the cold
   job so a long prepare never starves an established stream. It reports
   per-pass units/yield-points/frames and the minimum buffered runway rather
   than claiming hard real-time preemption. Tests: ≤4096 units, no-starvation,
   deadline-first+round-robin, between-unit cancellation, bounded pass + runway.

### Explicitly NOT done here (unchanged scope / pending gates)

- The full A4 streaming subsystem — stream rings, device/renderer consumption,
  worker OS thread, starvation fade/hold-cursor, FLAC seek loops, browser
  encoded preload — is NOT implemented. Only the scheduler discipline (the
  genuinely missing, offline-testable piece) landed. The rest stays A4-pending.
- Bus/modifier gains remain boundary-granular (not sample-ramped); modifier
  release is immediate (not fade-then-acknowledge). Recorded A3 limitations,
  unchanged.
- Real Linux device (A5), real browser AudioWorklet/worker (A6) and
  listening/quality/stress/SDK-consumer measurements (A7) remain pending; no
  device/browser/perf check is marked passed.

### Validation (this pass)

- Debug `ctest` 28/28 green; audio target **72 cases / 19,480 assertions**.
- ASan+UBSan leak-free across all 72 cases; TSan clean on SPSC flood + full
  suite. Zero warm submit/render allocation. clang-format + clang-tidy (pinned
  18) clean; all 5 public headers compile standalone. No regressions to
  Input/Text/Platform/RHI/Foundation.
