# Kiro handoff for Ludus audio

Use **Prompt 1** after these five files are in Kiro's checkout:

- `.kiro/specs/audio/requirements.md`
- `.kiro/specs/audio/design.md`
- `.kiro/specs/audio/tasks.md`
- `docs/architecture/audio-research.md`
- `docs/architecture/audio-kiro-handoff.md`

The package selects a small Ludus software mixer, private miniaudio device/codec/
resampler primitives, bounded voices/commands/buses, a separate streaming worker,
and native, Wasm AudioWorklet and offline adapters. It includes seven private
Gems chapter findings, a review of ten relevant chapters/selected sections from
Game Audio Programming **1, 3 and 4**, and current primary references. Volumes
2 and 5 were excluded. Kiro needs no PDF, book
figure or companion CD. This package proposes architecture; no audio code,
dependency acquisition, benchmark or playback verification has been performed.

The follow-up adds explicit preparation/voice transitions, bounded resident
concurrency groups independent of buses, split panning/attenuation listener
positions, incremental cold-worker decode, and an early audio gym with descriptor
validation and explainable silence. These changes are already integrated into
requirements/design/tasks; use the revised prompts below. Larger importance
directors, spatial world management and additional virtualization policies remain
extensions, with their rationale recorded in the research.

## Prompt 1 Implement the complete sequence

```text
Implement Ludus's audio system from this repository's specification. Read
AGENTS.md and applicable .kiro/steering/ first, then:
- .kiro/specs/audio/requirements.md
- .kiro/specs/audio/design.md
- .kiro/specs/audio/tasks.md
- docs/architecture/audio-research.md

Execute A0 through A7 in order, in small reviewable changes. This authorizes
implementation, not another architecture survey. Revalidate the actual checkout
and record existing changes. Preserve Input, Text, Platform, RHI, web smoke/probes,
shader work, other specs and unrelated user edits. Do not seek user approval
between ordinary stages or stop after generating a plan.

Implement the selected architecture: one instance-owned Ludus mixer, a single
control producer publishing bounded command batches, immutable prepared clips,
generation/session-checked playback handles, durable ownership acknowledgments,
a static bus tree with base/user gain and attenuation modifiers, bounded resident
virtualization, and a separate worker feeding preallocated stream PCM rings.
Add at most 16 resident concurrency groups with transactional MaxAdmitted and
deterministic MaxSelected quotas, independent of bus routing; report GroupCapacity.
Distinguish Pending/Scheduled/Mixed/Virtualizing/Virtual/Stopping/Terminal and
separate preparation readiness/retirement. Virtualizing fades can reverse when
selected within quota/capacity; Stopping cannot. Never reroll variation on reentry.
Use private miniaudio 0.11.23 low-level device, WAV/FLAC decoder and audited
resampler primitives. Do not add miniaudio's high-level engine/resource manager
or another gameplay voice layer. Lock actual source bytes/hash/license/options
and keep native/web dependency sources consistent. Never fabricate a hash.

Create Ludus::Audio with lightweight installed public headers and status/noexcept
APIs. Follow C++23, fixed-width aliases, no engine exceptions, fallible storage,
warning-clean builds, project formatting/tidy and header/include budgets. Root
CMake currently enables CXX only: integrate C deliberately with correct language
flags and static SDK dependency closure. Do not assume a Memory/jobs/VFS subsystem
exists or build one as a prerequisite.

Treat design ownership/cancellation rules as hard contracts. Keep allocation,
deallocation, I/O, locks, waits, logging, gameplay callbacks, decoder work and
unverified profiling out of rendering. Audit dependency call graphs. Validate
entire batches before publication, never split one across application boundaries,
and retain pins for pending plays. Stop and StopAll must work when the normal
queue is full. Stale commands/worker callbacks cannot target reused slots. Debug
ring loss cannot prevent terminal acknowledgment or asset reclamation.

Implement sample-clock starts, 128-frame control boundaries, exact output filling,
fractional cursors, audited rate conversion, explicit loops/EOF, persistent ramps,
mono meter-based panning, stereo beds, deterministic priority/audibility selection,
virtual loop advance and fade transitions counted within the 64-voice budget.
Keep selection envelopes out of selection scores. Support separate listener
panning/attenuation positions with a per-voice attenuation-origin choice, preserving
true emitter positions. Keep gameplay importance/director policy on the owner
side and send resolved numeric priority/gain/modifier commands to the renderer.
Streams remain protected/rate-1; starvation holds their source cursor, emits
fade/silence and reports lateness separately from EOF. Browser v1 preloads bounded
encoded bytes and streams decoded PCM; do not claim HTTP streaming memory bounds.
Cold worker decode yields after <=4096 prepared output frames to recheck refill
deadlines and cancellation. Measure blocking calls/runway and preparation latency
separately from applied Play and device latency; priority alone cannot preempt I/O.

Use the same production renderer offline, on Linux through a stable miniaudio
device owner, and on a pinned Emscripten Wasm AudioWorklet. Add a separate audio
web preset; preserve existing presets. Validate pinned 4.0.23 shared-memory/worklet
flags, isolation headers, actual context rate/block shape, stacks and memory.
Gesture resume and suspension must be explicit. Keep browser main-thread progress;
no ScriptProcessor fallback, per-block postMessage or main-thread joins.

In A0 prove async browser teardown with actual context close, worker/render
quiescence and late-startup cancellation. The SDK destroy-context helper only
suspends/releases its handle; do not infer it is safe to free buffers or stacks.
Suspended shutdown must not wait for another process callback. If an environment
or probe blocks a subgate, finish independent work and retain its pending status;
never weaken lifetime or real-time guarantees to get a green checkbox.

The private PDFs are intentionally unavailable to you. The provided research supplies
their findings, scope limits and modern adaptations. Do not request those PDFs or
historical CD code. Keep gameplay event/variation data above the mixer. Do not build
HRTF, acoustic simulation, reverb, surround, voice chat, a graph editor, DSP plugins,
GPU DSP, independent time stretch, general sound banks or global frameworks here.

Build numeric diagnostics and a small audio gym with the features, starting in
A1. Show independent silence causes, state/transition reasons, group admitted/
selected/fading counts, spatial inputs and score contributions. Validate event
descriptors and correlate failed preparation/admission with numeric tags without
creating phantom voices. Use existing owner-side terminal/HTML controls and
offline export; no UI dependency or callback-time breakpoint. Preserve published
command immutability and test producer floods plus both queue ownership edges.

Complete production-path correctness/failure/concurrency/allocation tests, real
Linux and browser playback, listening/resampler quality checks, trace/replay,
stress/callback/memory evidence, native/wasm consumers and pinned regression gates
in tasks.md. Mocks and offline/Node wasm cannot certify device/browser output.
Update the evidence ledger only for checks actually run; missing hardware tests
remain pending. Report changed files, API/build/demo usage, results and precise
remaining limitations. Do not push, merge or deploy unless separately instructed.
```

## Prompt 2 Implement one milestone

Use this instead of Prompt 1 to control scope. Begin with A0; substitute the
next milestone only after reading the previous result.

```text
Implement milestone A0 of Ludus's audio specification. Read AGENTS.md, applicable
.kiro/steering/, all files in .kiro/specs/audio/ and
docs/architecture/audio-research.md. The research already provides the private
PDF findings; do not ask for books or companion CDs.

Follow the selected design and this milestone's contracts. Revalidate the actual
checkout, preserve unrelated changes and existing native/browser systems, do the
production work/probes/tests for this milestone, and update the evidence ledger
with changed paths, exact commands/results and pending gates. Do not stop at
another plan, silently weaken requirements, mark missing device/browser checks
passed by mocks, or change dependencies without technical evidence. Do not
advance beyond this milestone, push, merge or deploy in this turn.
```

## Prompt 3 Review and repair the implementation

```text
Audit Ludus's implemented audio system against AU01-AU17 and A0-A7 under
.kiro/specs/audio/. Read AGENTS.md, steering, the design/research/decision log
and current production code. Checkboxes alone are not evidence.

Verify real-time call graphs including dependencies; batch transactions/queue
limits; single-producer assumptions and C++/wasm memory ordering; stop-full-queue
and stop-before-start; generation/epoch/session exhaustion; pending/active/fading
asset pins; durable terminal ownership; stream chunk/EOF/starvation/refill and
worker retirement; <=64 mixed voices including transitions; virtual cursor/
resampler phase; loops/rate/alias quality; scheduled frames/ramps; correct panning,
bus gain/mute/modifier composition and output clipping diagnostics.
Check transactional per-group admitted limits and selected quotas independently
from buses and the global cap; reversible virtualization versus final stop;
variation retention; selection-fade feedback; split listener and per-voice origin;
incremental cold work not starving refill; independent silence cause flags;
descriptor validation and audio gym reproductions. Verify publication/retirement
memory ordering and immutable consumer progress under producer floods.

Verify native device callback quiescence and loss/restart, actual browser shared
worklet/worker build, genuine gesture resume, suspension/processor failures,
late callbacks and async context close before reclamation. Check isolated and
missing-header behavior, no running memory growth, dependency/source parity,
SDK/static closure, public header budgets and preservation of existing systems.

Fix concrete violations within this scope, run pinned production-path checks,
and update the evidence ledger honestly. Require real playback and quality/
stress evidence; preserve missing hardware/browser checks as pending. Return
actionable findings, supported behavior and remaining gates. Do not push, merge
or deploy unless separately instructed.
```

## Prompt 4 Reconcile work started from the earlier handoff

Use this only if Kiro has already begun implementing the previous specification.

```text
Reconcile existing Ludus audio work with the revised requirements/design/tasks
under .kiro/specs/audio/ and the Game Audio Programming 1/3/4 follow-up in
docs/architecture/audio-research.md. Read AGENTS.md and steering first. Inspect
actual code and evidence; preserve completed conforming work and unrelated edits.
No private PDFs or volumes 2/5 are needed, and no new architecture survey is needed.

Implement missing explicit state transitions, resident concurrency-group admission/
selection, split listener, incremental cold-worker scheduling and explainable
diagnostics/descriptor validation/audio gym. Default group zero and coincident
listener positions preserve prior behavior. Keep the 64 mixed-voice hard cap,
durable retirement and callback restrictions. Test the new adversarial/state/
group/spatial/worker cases through production paths. Update milestone evidence
honestly; never mark unavailable device/browser or performance checks passed.
Report implemented deltas, validation and remaining gates. Do not push, merge
or deploy unless separately instructed.
```
