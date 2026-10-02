# Ludus audio research and decisions

Research date: 2026-10-01. Checkout inspected: `88b0b77a13b1688df6d1975d18cb1dfc6b4120fb`.
This is a design handoff, with no audio implementation or measured performance
claim. Read the [Kiro handoff](audio-kiro-handoff.md) for entry prompts and the
[design](../../.kiro/specs/audio/design.md) for implementation contracts.

## Recommended architecture

Build one instance-owned Ludus software mixer with a small command interface,
immutable resident PCM, bounded voices, a fixed bus tree, and a separate decode
worker. Use miniaudio 0.11.23 privately for native device access, WAV/FLAC decoding,
and its low-level resampling primitives. Use Emscripten's Wasm AudioWorklet API
for browser output. An offline adapter calls the same production mixer.

The main advantages are explicit ownership, bounded callback work, shared native
and browser signal processing, and reproducible failures. This is an engineering
judgment for Ludus, not a claim that a custom mixer beats commercial middleware
or every existing library. The benchmark and listening gates remain outstanding.

## Current primary sources

The following public sources were inspected. Source behavior and proposed Ludus
choices are separate: the contracts in the design are our decisions.

| Source | Evidence used | Ludus decision |
| --- | --- | --- |
| [PortAudio callback guidance](https://portaudio.com/docs/v19-doxydocs/writing_a_callback.html) | Allocation, I/O and mutex operations can have unbounded callback cost. | Permit bounded DSP and data movement; audit the entire callback call graph. |
| [miniaudio manual](https://miniaud.io/docs/manual/index.html) | The low-level API permits application mixing and callback output. Device objects require stable addresses. Device start/stop/uninit must run outside the data callback. | Private stable device owner; Ludus owns voices and mixing; control thread manages lifecycle. |
| [miniaudio 0.11.23 release](https://github.com/mackron/miniaudio/releases/tag/0.11.23) and [tagged source](https://raw.githubusercontent.com/mackron/miniaudio/0.11.23/miniaudio.h) | A released C dependency supplies device, decoder and linear-resampler APIs, feature switches and an MIT license option. | Start from this inspected release; A0 verifies hashes, enabled code and allocation behavior. Do not use floating master. |
| [SDL3 audio overview](https://wiki.libsdl.org/SDL3/CategoryAudio) | SDL3 centers its audio API on streams and conversion. | Viable alternative, but another platform dependency is unnecessary for Ludus's current Wayland/canvas stack. |
| [Wwise virtual voices](https://www.audiokinetic.com/en/library/edge/?id=concept_virtualvoices.html&source=SDK) and [FMOD resource management](https://www.fmod.com/docs/2.03/api/managing-resources-in-the-core-api.html) | Logical voices can survive without audible mixing; reentry policy matters. | Resident loops advance virtually; expendable short sounds can terminate. Keep stream behavior explicit. |
| [Emscripten AudioWorklets](https://emscripten.org/docs/api_reference/wasm_audio_worklets.html) | Wasm can perform DSP on the audio thread. Current documentation also describes modes newer than Ludus's SDK. | Validate against the pinned SDK, not the latest documentation alone. |
| [4.0.23 Web Audio header](https://raw.githubusercontent.com/emscripten-core/emscripten/4.0.23/system/include/emscripten/webaudio.h) and [settings](https://raw.githubusercontent.com/emscripten-core/emscripten/4.0.23/src/settings.js) | The pinned version exposes async worklet setup and context resume, planar output frames, and AUDIO_WORKLET support depending on WASM_WORKERS/shared memory. Its post-function helpers generate JS garbage; destroy-context is documented as suspension plus handle release. | Dedicated audio web preset, shared queues, no per-block postMessage, and explicit async close/quiescence work. Handle release alone cannot justify freeing callback storage. |
| [AudioWorklet process contract](https://developer.mozilla.org/en-US/docs/Web/API/AudioWorkletProcessor/process) and [Web Audio specification](https://webaudio.github.io/web-audio-api/) | Output block shape comes from the host; processing participates in a separate audio rendering timeline. | Inspect supplied channel/frame counts, adapt layout once, and keep the sample clock separate from game ticks. |
| [Web Audio autoplay](https://developer.mozilla.org/en-US/docs/Web/Media/Guides/Autoplay) | Programmatic audio is subject to browser autoplay policy. | Explicit gesture resume and observable suspension; no busy wait. |
| [Steam Audio C API](https://valvesoftware.github.io/steam-audio/doc/capi/getting-started.html) | Binaural rendering uses HRTFs and per-effect state. | Credible later spatialization adapter; stereo panning initially. |

The pinned 4.0.23 settings/header were also found in the local webgpu-w0 tooling
checkout. The primary checkout currently has no installed emsdk at the preset's
usual path. Source inspection establishes API availability, not a working audio
binary, safe shutdown, browser compatibility, or performance.

## Private Gems articles actually read

Discovery used `references/game-dev-gems-toc.md`, searching audio, sound, music,
mixing, streaming and voice topics. Seven chapters were read from the local
PDFs using text extraction. The category/snapshot diagrams in Sparks and the
loading-trigger page in Franco were also rendered and visually inspected.
PDF page numbers below count the first PDF page as 1 and differ from printed
numbers. These are original paraphrases; no source code, book pages, figures,
or private PDFs need to be copied into Kiro's checkout.

### Scott Patterson and game audio design patterns

*Game Programming Gems 2* (2001), chapter 6.1, printed pp. 514-520,
PDF pp. 491-497, “Game Audio Design Patterns.”

The chapter separates sound identifiers from individual playback handles and
uses a facade to hide subsystem complexity. It discusses queued commands,
group control, status queries, diagnostic logging and disabling audio to isolate
problems. Apply the handle separation, small API, inspectable commands and
explicit disabled mode. Replace historical untyped IDs with typed session and
generation checks. Return numeric snapshots rather than strings built on the
audio thread. Deliver completion information through polling on the control
thread rather than invoking gameplay callbacks during rendering.

### Keith Weiner and the processing pipeline

*Game Programming Gems 2* (2001), chapter 6.4, printed pp. 529-538,
PDF pp. 506-515, “Interactive Processing Pipeline for Digital Audio.”

The output consumer determines the required frame count. Processing order
matters, input consumption can differ from output production, and EOF is
distinct from effect-tail completion. The chapter acknowledges overflow and
lifetime limitations in its general pipeline. Apply exact output filling,
explicit consumption/production counts, separate source EOF and starvation,
and preallocated persistent processing state. Do not copy its retry-until-full
loop or backfill-buffer machinery into the callback. The first Ludus renderer
has fixed-rate stages and bounded resampling, with no general tail-producing
DSP graph. Future effects must define bounded tails and work before integration.

### Ian Lewis and the low level API

*Game Programming Gems 2* (2001), chapter 6.7, printed pp. 559-560,
PDF pp. 536-537, “A Low-Level Sound API.”

Shared wave storage and a separate per-play cursor allow multiple instances
without duplicating sample data. A mixer abstracts the output architecture.
Apply immutable clips and independent voice cursors behind a private adapter.
Do not adopt DirectSound inheritance trees, frame-driven mixing, historical
thread-scheduling assumptions, or deletion on the audio path. Device callbacks
drive our mixer independently of render and simulation frame rates.

### Jason Page and MultiStream

*Game Programming Gems 7* (2008), chapter 4.2, printed pp. 305-319,
PDF pp. 338-352, “MultiStream—The Art of Writing a Next-Gen Audio Engine.”

The chapter connects codec/buffer memory, streaming deadlines, loop prefetch,
processing granularity, volume smoothing and bus DSP cost. It shows how an
audio update between related commands can put layers out of phase. Apply one
atomic command batch, explicit frame/byte budgets, ramps, and worker-side
prefetch. Prioritize existing stream deadlines before new preparation. Use
shared bus processing where appropriate. Do not import PS3/SPU/DMA mechanisms,
512-voice targets, optical-disc duplication, or its historical codec choices.
Its FFT/window examples are particular overlap-processing choices, not universal
FFT requirements or a reason to put every voice through an FFT. The first
Ludus mixer needs neither frequency-domain processing nor GPU scheduling.

### Robert Sparks and context driven mixing

*Game Programming Gems 7* (2008), chapter 4.5, printed pp. 341-347,
PDF pp. 374-380, “Context-Driven, Layered Mixing.” The local catalog omits
the comma in the title.

Categories collect related sounds. Snapshots specify category controls, and
scene activation adds/removes snapshots with transitions. The example separates
pre-mix, mutually exclusive base, and overlapping modifier layers. Apply a small
bus tree, one base mix and bounded attenuation modifiers with a documented
composition rule. Expose active modifiers and final gains in the debug view.
Keep user volume separate so gameplay never defeats a user mute. Do not copy
the remote MIDI tool, packed parameter optimization, arbitrary effect controls,
or a full mixing application before Ludus has a consumer.

### Mat Noguchi and audio team workflows

*Game Programming Gems 8* (2010), chapter 6.2, printed pp. 553-562,
PDF pp. 568-577, “Empowering Your Audio Team with a Great Engine.”

The chapter gives designers parameterized sounds, distance envelopes, variations,
loop transitions, sound classes and cumulative mix controls. It also explains
instance cascades that replace many similar particle sounds with aggregate
content. Apply explicit units, data-selected assets and small event descriptors
above the renderer, variation selection before publication, concurrency limits,
and inspectable bus controls. Avoid engine-side knowledge of vehicles, particles
or character classes. Cascades, elaborate loop-state machines and authoring UI
remain later content features rather than core mixer obligations. Do not copy
substring-based sound-class selection; use typed bus IDs.

### Simon Franco and sound pack organization

*Game Engine Gems 2* (2011), chapter 21, printed pp. 359-367,
PDF pp. 375-383, “Data-Driven Sound Pack Loading and Organization.”

The chapter uses emitter co-occurrence and audible overlap to distinguish
resident/streamed content, group packs and estimate memory pressure. Loading
regions extend beyond audible regions to account for load time and listener
motion. Apply separate residency modes, simultaneous-use memory estimates,
explicit preparation, and prefetch from measured latency. Do not turn its
heuristics into fixed runtime laws or copy its optical-storage assumptions.
An offline bank dependency list is sufficient initially; world partitioning,
overlap analysis and spatial loading belong to a future asset/world system.

## Catalog entries that were not read

The catalog's *Game Programming Gems 6* “Real-Time Mixing Busses” by James Boer
is relevant, but the local PDF did not expose searchable chapter text in the
attempted extraction. No finding here is attributed to that chapter.
Game Audio Programming volumes **1, 3 and 4**, followed by **2**, were added
locally and reviewed below. Volume **5** is unavailable and remains excluded.
No finding is attributed to it. Chapters not listed
below, including other catalog entries on automated testing, remain reading
pointers rather than evidence. Kiro needs neither these PDFs nor companion CDs.

## Follow-up: Game Audio Programming 1, 3 and 4

Review date: 2026-10-01. Sources are the local private PDFs
`references/Game Audio Programming 1.pdf`, `references/Game Audio Programming 3.pdf`
and `references/Game Audio Programming 4.pdf`. Ten relevant chapters were
reviewed: their architecture prose and applicable implementation sections, with
selected sections explicitly identified below. This is not a cover-to-cover
review of three books. Text was extracted locally; volume 1's virtualization
state diagram (Figure 2.5, PDF p. 44), split-listener diagram/table (Figure 11.8/
Table 11.5, PDF p. 228) and volume 4's debug-menu example (Figure 16.2, PDF p. 325)
were rendered and visually inspected. Scratch extractions/renders stay outside
the handoff. The findings below are original paraphrases, with no copied code.

### Review decisions

| Finding | Change to the proposed Ludus design | Implementation/acceptance location |
| --- | --- | --- |
| Loading, virtualization and stopping have different legal transitions | Separate preparation from playback; distinguish reversible Virtualizing from final Stopping; preserve chosen variation on reentry | Design 3/7; AU06/AU07/AU10; A1/A2 |
| A single physical cap does not keep similar sounds from overwhelming a mix | Add at most 16 resident concurrency groups with separate admitted/selected limits, independent of buses; retain the global 64 hard cap including tails | Design 7; AU05/AU10; A1/A2 |
| Third-person distance and panning can need different origins | Add two listener positions and a per-voice attenuation-origin choice; retain true emitter coordinates | Design 6; AU11; A2 |
| Context changes importance; it is not a universal engine rule | Application resolves priority/gain/modifiers; renderer executes numeric commands and ramps; expose score contributions | Design 7/8; AU10/AU12/AU16; A2/A3 |
| Published command ownership matters more than the number of buffers | Retain fixed SPSC rings; require immutable publication, retirement edges and producer-flood progress tests | Design 4/11; AU03/AU05; A1 |
| Streaming deadline priority can be defeated by one long cold decode | Bound cold decode units to 4096 prepared frames, recheck refill/cancellation between units, and measure runway plus blocking call durations | Design 9; AU13; A4 |
| Diagnostics and authoring validation should arrive with the feature | Add silence cause flags, score/group/spatial details, descriptor validation and an early audio gym using existing terminal/HTML output | Design 9/11; AU16/AU17; A1-A7 |

The common mixer, private miniaudio dependency, single control owner, one decode
worker, offline renderer and browser worklet remain the recommendation. The
readings refine policy and observability; they provide no measured evidence
that this proposed implementation meets its CPU, memory or listening targets.

### Volume 1, chapter 2: explicit state transitions

Guy Somberg, “Sound Engine State Machine,” printed pp. 13-30, PDF pp. 36-53.

Adding asynchronous loads, fades and virtualization to a jukebox-style API can
turn loosely related flags into ambiguous behavior. The chapter makes states
and transitions explicit. In particular, a virtualization fade can reverse
when a sound becomes eligible again, while a requested stop completes its
lifetime. Devirtualization retains previously selected randomized parameters.

Apply those distinctions to Ludus's state table and transition tests. Preparation
already happens before Play, so do not add an implicit Loading state to live
voices or copy the book's frame-driven middleware update loop. Keep stop-before-
start and late-result cancellation governed by durable ownership. The book's
suggested fade durations are game-specific; retain our configurable 5 ms default
until listening evidence justifies another value.

### Volume 1, chapter 3: preparation and refill deadlines

Blair Bitonti, “Streaming Sound,” printed pp. 31-47, PDF pp. 54-70.

The chapter distinguishes stream resource limits from resident sounds, relates
I/O scheduling to time until data is needed, and explains selectively priming a
stream's head to avoid a read at its trigger. It also stresses that in-flight
I/O retains buffer ownership. Much of its tuning concerns optical media.

Our Ready contract already supplies decoded head data and keeps streams protected.
Make cold-worker decode incremental so deadline-first scheduling remains useful
during large prepares. Measure readiness latency, buffered runway and output
latency separately. Preserve cancellation/worker retirement and preprepare
latency-critical dialogue. Do not add a second primed cache, copy historical
256 KiB encoded buffer constants, adopt volatile reference counts, or promise
that private worker priority controls an OS file scheduler.

### Volume 1, chapter 11: split listener

Guy Somberg, “Listeners for Third-Person Cameras,” printed pp. 197-208,
PDF pp. 220-231.

A camera-position listener preserves screen-relative direction but can give
undesired attenuation in third person. A player-position listener can fix
distance while spoiling direction. The chapter separates attenuation origin
from the panning pose, allows authored exceptions such as footsteps, and warns
against using fabricated middleware positions for obstruction calculations.

Our mixer can evaluate the two displacements directly, without fabricating a
source location. Default origins coincide; applications supply camera/player
positions explicitly and choose the per-voice exception. Test those independent
effects and keep true emitter coordinates visible. This improves stereo spatial
policy; it does not add front/back cues, surround, multiple listeners or acoustics.

### Volume 3, chapter 9: resource and perceptual limits

Robert Gay, “Voice Management and Virtualization,” printed pp. 133-142,
PDF pp. 156-165.

The chapter separates physical resource pools from perceptual limits on sound
families. A global cap is a final resource defense rather than a complete mix
policy. It describes selection rules, revival behavior, transition capacity,
cursor-local coarse loudness metadata and coordination with streamed residency.

Add small resident concurrency groups alongside our existing budgets. One group
per voice, fixed session limits and advance/kill policies keep the first version
readable. Trace group rejection/selection and charge fades globally. Do not add
overlapping pool membership, automatic stream virtualization or dynamic rule
graphs. A whole-clip peak can overvalue quiet tails; document that limitation
and reserve cursor-local envelopes for a measured refinement. Visibility must
not implicitly decide audibility.

### Volume 3, chapter 12: context and readable selection

Guy Somberg, “An Importance-Based Mixing System,” printed pp. 181-203,
PDF pp. 204-226. Reviewed the scoring/bucket architecture (12.1-12.3), relevant
state/fader implementation and debug-display discussion (12.4.5-12.5).

Importance depends on game context, such as which opponent targets the player,
rather than only a static sound category. The chapter ranks relevant entities,
assigns effect buckets, interpolates changes and displays score contributions
so designers can understand the result.

Keep the gameplay calculation above Audio; use existing numeric priority/gain
updates and modifiers. Show our own audibility-score factors separately and do
not multiply selection fades back into selection. A future importance director
can use the same seam. Do not embed actors, weak pointers, threat queries,
per-event EQ creation or a general bucket-effect system in the real-time mixer.
The chapter's particular scoring/effects are examples, not Ludus defaults.

### Volume 4, chapter 1: emitter management boundary

Christian Tronhjem, “Audio Object Management Techniques,” printed pp. 3-23,
PDF pp. 22-42. Reviewed pooling, position updates, grouping, spatial-partition
tradeoffs and the example implementations.

Reusable objects reduce allocation churn, but they need a reliable return path.
Static emitters do not need repeated positions; short sounds can retain their
initial pose, and distant objects may tolerate fewer updates. Large worlds can
activate nearby sources through spatial queries, while small arrays can outperform
trees. Bounds must contain the audible region, not just the visible point.

Our fixed voice slots and terminal acknowledgments already supply pooling.
Document change-only control-side transform updates and application-owned
active-handle tracking. Keep a contiguous bounded renderer rather than adding
an emitter object per world entity. Grids, aggregation and update-rate LOD belong
to an eventual application/world layer, with profiling and audible-range
fixtures. Do not copy unchecked pool/grid indexing or infallible growing storage.

### Volume 4, chapter 2: dynamic mix policy

Colin Walder, “State-Based Dynamic Mixing,” printed pp. 24-39, PDF pp. 43-58.

The chapter frames a dynamic mix as identifying affected sounds, deciding from
context, then applying actions. Actions need transition behavior; separate state
owners can contribute to the mix. Its Cyberpunk examples include tags, dB offsets,
event replacement and a narrative state-graph director.

Clarify that Ludus applications resolve decisions before publishing numeric
commands; renderer-owned ramps execute the resulting values. Preserve independent
modifier ownership and user mute. Existing dB attenuation modifiers cover the
small useful subset. A narrative director, tag-query engine, distance remapping
and event-replacement cascade remain later application features. They do not
justify putting game condition evaluation or an editor into the mixer.

### Volume 4, chapter 12: command-buffer ownership

Guy Somberg, “Thread-Safe Command Buffer,” printed pp. 238-261,
PDF pp. 257-280.

The chapter builds a three-buffer exchange with explicit sender/receiver
ownership, alternating allowed operations and publication ordering. It also
discusses why repeatedly withdrawing published data to replace it can prevent
receiver progress. Its final ownership protocol, rather than the number three,
is the relevant concurrency lesson.

Retain our preallocated SPSC command/snapshot rings and document both publish
and retirement edges. Keep accepted gameplay batches immutable; coalesce only
owner-private unpublished updates. Add producer-flood/interleaving tests. Debug
snapshots may skip publication, while commands return QueueFull and ownership
acknowledgments remain durable. Do not introduce dynamically growing vector
buffers, replace a proven ring without evidence, or claim every triple buffer
is either automatically safe or inherently unsafe.

### Volume 4, chapter 13: tools and content validation

Matias Lizana García, “Optimizing Audio Designer Workflows,” printed pp. 265-278,
PDF pp. 284-297. Reviewed 13.1-13.3.6 (printed pp. 265-269/PDF pp. 284-288)
and the later event/data separation discussion (printed pp. 275-278/PDF pp. 294-297).

The selected sections motivate validators for recurring setup failures,
inspectable configuration/references, and an audio gym that assembles reproducible
feature scenarios. Actions and object data should be separate so designers can
reuse behavior across materials or object kinds.

Add a small gym and descriptor validation with each milestone. Correlate numeric
tags with failed admission and selected assets. Application descriptors represent
actions plus explicit data; the core remains a small typed playback API. Do not
copy a singleton event manager, build a general gameplay event bus, or promise
automatic naming/tag pipelines before there is an authoring consumer.

### Volume 4, chapter 16: observable behavior from the start

Stéphane Beauchemin, “Audio Debugging Tools and Techniques,” printed pp. 295-309,
PDF pp. 314-328.

The chapter argues for diagnostics developed with a feature, rather than a
late flood of logs. Filtered views, source positions, parameter history,
capture controls and an event-specific breakpoint shorten investigation.
Its demonstrations use Unreal, Wwise and ImGui.

Extend our snapshots with independent silence causes, group limits, spatial
inputs and selection factors; build owner-side filtering and named reproductions
from A1. Keep breakpoints, formatting and files outside rendering. Use existing
terminal/HTML tools first, with an optional later UI consumer of the same records.
No ImGui dependency, raw object-pointer clipboard feature or callback-time
breakpoint is needed. The book's middleware/engine version details do not
establish current compatibility for Ludus.

## Follow-up: Game Audio Programming 2

Source: local private `references/Game Audio Programming 2 Principles and Practices.pdf`.
Eight relevant chapters/selected sections were reviewed, as scoped below, bringing
the Game Audio Programming review to eighteen chapter entries across volumes 1-4.
This is not a cover-to-cover review. Printed/PDF locators are one-based; in this
copy PDF page = printed page + 27. Figure 19.2 (printed p. 310, PDF p. 337) and
Figure 13.1 (printed p. 227, PDF p. 254) were rendered and visually inspected.
The curve and presentation-boundary findings below are original paraphrases;
no extracted pages, figures, example code or private PDFs enter the Kiro package.

### Volume 2 review decisions

| Finding | Ludus decision | Implementation/acceptance location |
| --- | --- | --- |
| Sliders need an intentional gain curve and exact mute | Add an owner-side normalized-to-dB example, initially 40 dB; keep UserGain amplitude-based and player settings separate | Design 8; AU12; A3 |
| Muted finite voices can become stale playback backlogs | Reinforce natural expiry/kill policy, retained PCM pins and mute-cycle reclamation tests | Design 7/11; AU10/AU16; A2 |
| Concurrency caps do not suppress successive duplicate events | Add a bounded application cooldown/AlreadyActive example before admission, with explicit clock and rollback | Design 9; AU16; A3 |
| Presentation and mix-state owners need clear lifetimes | Copy typed action data; stop owned loops on entity removal; release shared dialogue duck only after the last durable terminal | Design 8/9; AU12/AU16; A1/A3 |
| Buffer sizes do not determine resampling ratios or channel meaning | Specify rate-derived ratios, continuous phase/history and checked frame/layout capacities; test partial tails and adapter parity | Design 5/6; AU08; A2 |
| Meters can mislead without taps, units and windows | Specify per-channel input/post-gain peak/RMS, current/target gain and owner-side dBFS display | Design 8/11; AU12/AU16; A3 |
| A mixer thread plus FIFO is an architectural tradeoff | Retain bounded direct callback DSP; defer output mixing ahead until measured need and explicit latency/underflow/quiescence contracts | Design 13; AU04; A0/A7 |

### Volume 2, chapter 19: useful volume controls

Guy Somberg, “Implementing Volume Sliders,” printed pp. 307-316,
PDF pp. 334-343.

Linear amplitude does not give evenly spaced dB changes. A normalized slider
mapped through an explicit dB interval gives a useful predictable control, with
zero treated separately as exact mute. Adopt a 40 dB initial range: halfway is
-20 dB (amplitude 0.1), full is 0 dB, and zero mutes. Convert on the control
owner and use existing gain ramps, including at the discontinuous zero endpoint.
Persist slider position and a separate mute flag rather than the composed mix
gain, so gameplay modifiers cannot erase the player's preference.

The curve/range is a tunable application choice, not a universal perceptual law.
Do not adopt the chapter's master/music-only control recommendation: Ludus leaves
category and accessibility controls to the application. No new render-time
slider subsystem is needed.

### Volume 2, chapter 9: finite virtual voices and ownership

Nic Taylor, “Understanding Wwise Virtual Voices,” printed pp. 147-156,
PDF pp. 174-183.

Threshold-based virtualization can result from bus mute, mix state or distance,
not just a quiet sample. Restart/resume behavior for finite fire-and-forget cues
can retain old sounds and make them reappear after an unmute or scene change.
Use the chapter's failure cases to strengthen Ludus acceptance tests: advancing
finite voices reach EOF while muted; kill-policy cues terminate; repeated context
cycles create no stale burst or retained-slot/pin growth.

Keep the existing two policies. Do not add paused/restart virtual modes merely
because middleware supports them, or silently derive policy from a bus. Ludus
resident virtualization avoids DSP but retains shared prepared PCM; it is not
asset eviction. Expose virtual age and ownership counts. The chapter's historical
Wwise versions and buffer-flushing behavior do not define Ludus stream semantics;
streams remain protected, consuming their prepared PCM at rate 1 under mute.

### Volume 2, chapter 5: resampling across unequal buffers

Guy Somberg, “Audio Resampling,” printed pp. 85-96, PDF pp. 112-123.

Interpolation need not materialize a large least-common-multiple-rate stream.
However, a buffer-length ratio only describes a rate ratio when both buffers
represent the same time interval. Real partial chunks, history and tails violate
that assumption. Specify source/session rates and playback Rate independently
of buffer sizes; retain fractional phase, history and consumed/produced counts
across calls. Guard neighbor reads at EOF and loop seams, and test 44100/48000
conversion, tiny tails and alternate output partitions.

The pedagogical implementation is not a production kernel: per-call phase reset,
unguarded next-sample access and absent anti-alias filtering require additional
work. Retain the audited preallocated low-pass resampler and quality gates;
neither copy the example nor call unfiltered linear interpolation high quality.

### Volume 2, chapter 13: a thin presentation adapter

Jon Mitchell, “Techniques for Improving Data Drivability of Gameplay Audio Code,”
printed pp. 225-234, PDF pp. 252-261.

A presentation layer can translate game model values into audio actions and
parameters without coupling sound-engine objects to every game object. Adopt
that boundary as a small application adapter: typed actions/data, copied resolved
values, prepared descriptors and tracked accepted handles. Detached one-shots
can finish after entity removal; owned persistent loops must stop. Context owners
release their modifiers at teardown. Never pass game pointers into rendering.

Do not require the book's MVVM machinery, parameter/remapping trees or a general
RTPC language. Neighborhood mixing, spatial hashes and staggered updates are
large-world extensions; the chapter's update cadence is an example, not a Ludus
timing constant. The common mixer remains independent of ECS and world queries.

### Volume 2, chapter 14: suppression before admission

Akihiro Minami and Yuichi Nishimatsu, “Data-Driven Sound Limitation System,”
printed pp. 235-243, PDF pp. 262-270.

Content policies can reject duplicate/recent events, limit categories or attenuate
other categories. Ludus groups already cover simultaneous admission/selection;
they do not stop a fast sequence of short repeated triggers. Demonstrate a
fixed-capacity application table keyed by numeric event/owner, with explicit
cooldown and AlreadyActive suppression before slot/pin reservation. Count accepted
Pending/virtual handles, not a lagging Mixed snapshot. Update policy state only
after successful admission; engine failure must not consume cooldown, leave
phantom activity or create a duck. Record suppression reasons and the chosen
application clock; table exhaustion is an explicit application failure.

For overlapping dialogue, own one shared attenuation context and remove its
modifier only after the final accepted member durably terminates. Never restore
category gain to 1 on Stop, which would override user gain or another context.
Preserve batch rollback and durable completion despite dropped diagnostics. A
runtime macro interpreter, class per policy command and renderer gameplay query
system are unnecessary for these small examples.

### Volume 2, chapter 15: mix intent and trustworthy meters

Tomas Neumann, “Realtime Audio Mixing,” printed pp. 245-257, PDF pp. 272-284.

Importance, audibility, resource limits and game state are related but distinct
mix inputs. Existing numeric priority, group quotas and owner-resolved modifiers
fit this distinction. State lifetime needs explicit cleanup so abandoned owners
do not leave attenuation active. Strengthen the metering contract: per-channel
peak/RMS input and post-gain taps, fixed windows, actual ramped versus target
gain, and explicit dBFS conversion/silence on the owner. Child gain is already
included at its parent's input; each ancestor still applies once.

Do not infer perceptual loudness or true peaks from sample peak/RMS. Automatic
HDR windows, signal-driven compression and spectral analysis remain extensions
with consumers and quality/cost evidence. A captured offline signal can support
later analysis without adding FFT or display work to the callback.

### Volume 2, chapter 3: direct mixing versus output mixing ahead

Dan Murray, “Multithreading for Game Audio.” Reviewed printed pp. 33-42 and
49-56, PDF pp. 60-69 and 76-83: sections 3.1-3.4, the opening of 3.5, section
3.6 and Appendices A/B. The remaining worker discussion and platform Appendix C
were not reviewed.

Device pull must progress independently of game frames. A producer mixer thread
and PCM output ring can isolate expensive processing, but its buffering adds
output latency and another scheduling/ownership boundary. Coherent command
publication matters in either architecture. Preserve Ludus's bounded batches,
immutable publication and limited application work; do not copy examples that
ignore push failure or drain indefinitely. Decode-source prefill is distinct
from an output FIFO.

Retain direct bounded DSP in the device/worklet callback. This is compatible
with [PortAudio's callback guidance](https://portaudio.com/docs/v19-doxydocs/writing_a_callback.html),
whose example generates samples there while its guidance excludes potentially
unbounded calls. It does not prove Ludus performance. A mixer thread is a later
measured alternative with bounded FIFO, underflow/timeline, latency and teardown
contracts. Spinlocks, semaphore waits and priority assumptions cannot substitute
for bounded renderer work or measured scheduling evidence.

### Volume 2, chapter 4: channel meaning and layout

Ethan Geller, “Designing a Channel-Agnostic Audio Engine.” Reviewed printed
pp. 61-67 and 81-84, PDF pp. 88-94 and 108-111: introductory format/layout
discussion and concluding design tradeoffs. The intervening graph implementation
was not reviewed.

A channel count is insufficient to identify a speaker arrangement or encoded
soundfield. Explicit formats and layout conversions prevent plausible-looking
but incorrect output. Tighten Ludus's small PCM contract: mono/stereo meaning,
interleaved/planar layout, frames versus sample values and checked capacities.
Adapt through private boundaries, with distinct L/R impulse tests across offline,
native and worklet output. Reject unsupported layouts instead of guessing.

This does not require arbitrary channel graphs, intermediary panning formats,
ambisonics or plugin mixer hierarchies. Keep stereo v1; a future format consumer
must bring routing, speaker semantics and quality/CPU fixtures.

## Repository fit and alternatives

There is no Audio module, general job system, asset/VFS layer or implemented
FoundationMemory in the inspected checkout. ADR 0008 is proposed. Existing
fallible `Array` storage and private nothrow allocation are sufficient. Input,
Platform, RHI, native Text and browser smoke/probes already exist. Audio needs
none of their private graphics handles. Root CMake declares CXX only. Enabling C
for a pinned C dependency is an explicit integration task, with language-specific
flags and native SDK static-link closure checked.

Normal Logging locks around sink delivery; the web logger assumes a main-thread
caller. Neither normal logging nor unverified profiler registration belongs in
audio rendering or browser decode workers. Publish bounded numeric diagnostics
and drain them on the owner thread instead.

| Alternative | Why it is not selected now | Revisit trigger |
| --- | --- | --- |
| miniaudio high-level engine owns everything | Attractive for fastest integration, but combines another voice/node/resource manager with the precise Ludus admission, lifetime and replay contract | Maintenance cost of the small mixer exceeds its measured benefit; compare a production-path prototype |
| FMOD or Wwise | Strong authoring and sophisticated mixing; integrating an external runtime and content toolchain is a product commitment | A designer needs those workflows and the project chooses the integration/distribution terms |
| Direct PipeWire/ALSA/WASAPI implementations | More backend code and recovery work with little immediate game benefit | A verified miniaudio backend limitation affects shipping behavior |
| SDL3 audio | Useful for an SDL platform stack; currently duplicates platform dependency surface | Ludus adopts SDL for broader reasons or a device defect justifies it |
| OpenAL source model | Delegates many signal-processing decisions, making shared offline/browser behavior harder to keep identical | A target platform specifically benefits from it |
| Audio graph editor and plugin DSP | Requires graph compilation, resource retirement, tails and latency compensation | A second real DSP consumer requires routing beyond a bus tree |
| HRTF and acoustic propagation | Stereo pan lacks front/back/elevation cues; these would improve headphones/3D worlds | A 3D/VR consumer supplies fixtures and a CPU/platform budget |
| GPU DSP or handwritten SIMD everywhere | Adds synchronization, portability and debugging cost before scalar evidence exists | Profiled DSP cost justifies a targeted kernel with scalar parity tests |

The selected renderer is intentionally small enough to inspect. State of the art
here means predictable deadlines, audible continuity, explicit failure, useful
tools and measured quality; it does not require every contemporary audio feature.
