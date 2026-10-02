# Ludus audio requirements

Status: proposed implementation contract, 2026-10-01. No requirement is
implemented or validated by this handoff. The [design](design.md),
[tasks](tasks.md), and [research](../../../docs/architecture/audio-research.md)
make this package independent of the private books.

## Scope

Deliver a small stereo game-audio system for the supported Linux host and an
explicit browser audio build, with the same mixer runnable offline. Support
resident effects, looping ambience, streamed music, mono positional panning,
category buses, mix transitions, bounded voices and useful diagnostics. Keep
gameplay and content policy above real-time signal processing. HRTF, reverb,
microphones/voice chat, surround, DSP plugins and music sequencing are deferred.

## Required behavior

| ID | Requirement and acceptance condition |
| --- | --- |
| AU01 | Follow AGENTS.md, applicable steering and ADRs. All engine paths are exception-free, explicit-status and `noexcept` where appropriate. Use Ludus types, lightweight public headers and fallible allocation. Preserve existing code and unrelated changes. |
| AU02 | Expose one instance-owned AudioSystem with explicit lifecycle, typed clip/stream/voice handles, session/generation validation, no global service locator and no native/dependency types in the SDK. Offline mode exercises the production mixer. |
| AU03 | Exactly one control owner publishes commands. The renderer owns live voice cursors, DSP and bus state; one decode worker owns decoder/I/O state. No unsynchronized shared structures, multiple producers on an SPSC queue, or caller pointers retained beyond documented lifetime. |
| AU04 | Rendering and warm command submission allocate/free nothing and use no mutex, I/O, waits, normal logging, gameplay callbacks or unverified profiler facilities. Bound commands, voices, scratch, snapshots, streaming and transition work. Audit dependency calls, not just Ludus wrappers. |
| AU05 | Validate the whole command batch before publication. Accepted batches apply together at one control boundary; rejected batches cause no partial playback or reference retention. Published payload stays immutable until consumption; producer update floods cannot retract work or starve consumer progress. QueueFull and capacity failures are observable; accepted means queued, not audible. |
| AU06 | Individual Stop and StopAll remain deliverable with a full command queue. Stop-before-start cancels pending playback. Delayed/stale commands cannot affect reused slots or assets. Completion notification loss cannot lose ownership or prevent reclamation. |
| AU07 | Prepare resident PCM outside rendering, own its bytes, share immutable data across instances and retain it for pending/active/fading uses. Retire assets explicitly and reclaim only after renderer and worker acknowledgment. Restart invalidates old handles. |
| AU08 | Render requested frames completely, with explicit mono/stereo meaning, interleaved/planar layout and checked frame/sample capacities, finite output, ramps, tested loop boundaries, EOF handling and source/output-rate conversion. Derive ratios from rates, preserve fractional phase/history across partial buffers, and test noninteger conversions and adapter channel parity. Resident playback rate supports 0.5-2.0 through an audited bounded resampler; streamed playback remains rate 1.0. Quality limits are documented and measured. |
| AU09 | Use a rendered-sample clock independent of simulation steps. Batches can synchronize resident starts at an explicit sample frame within a bounded horizon. Report late starts. Device suspension freezes transport; disabled and explicit offline modes have distinct clock semantics. |
| AU10 | Bound logical and mixed voices separately from perceptual concurrency groups. Up to 16 resident groups have transactional MaxAdmitted and deterministic MaxSelected quotas, independent of bus routing; rejected admission returns GroupCapacity. Apply priority/audibility selection, stable ties, hysteresis and charged fades. Virtual loops advance without DSP; advancing finite sounds expire even while muted, and expendable sounds can terminate. Virtual residents retain capacity charges and shared PCM pins until acknowledged; virtualization is not PCM eviction. Explicit voice/preparation state machines distinguish reversible Virtualizing from irreversible Stopping, retain chosen variations on reentry and forbid late loading resurrection. Streams have a protected policy; all limits and reasons are visible. |
| AU11 | Mono emitters use explicit meter coordinates, validated listener orientation, bounded distance attenuation and equal-power stereo panning. Separate panning and attenuation positions default to the same pose; a per-voice attenuation-origin choice supports camera-relative exceptions. Preserve true emitter positions and stereo beds. Coincident, behind-listener, invalid, third-person and out-of-range cases are tested. Do not claim binaural or physical acoustics. |
| AU12 | Use a static bus tree configured before rendering, user gain separate from gameplay mix, one base mix and bounded attenuation modifiers. Changes ramp; composition is specified. Demonstrate owner-side dB slider mapping with exact mute and remembered settings. Overlapping dialogue releases its owned duck only after the last accepted member terminates; admission failure creates no duck. Expose per-channel input/post-gain peak/RMS with documented windows/taps, clipped frames, active modifiers and current/target gain; dBFS display does not claim LUFS/true-peak measurement. |
| AU13 | WAV/FLAC preparation and stream decoding use pinned common sources. Worker decode/I/O publishes preallocated PCM chunks. Recheck stream deadlines/cancellation between cold units of at most 4096 prepared output frames; measure blocking call duration and buffered runway. Prefill before PlayStream, measure preparation separately from playback latency, distinguish starvation from EOF, and fade without waiting for absent data. Bound compressed and decoded memory independently. |
| AU14 | Linux output uses a private miniaudio adapter. No-device initialization reports failure; fallback to Disabled requires caller policy. Native device loss closes admission, retires voices and recovers through explicit control-thread restart. Real device verification is separate from offline tests. |
| AU15 | Browser audio uses a pinned Emscripten Wasm AudioWorklet/shared-memory build and private decode worker. Preserve current web presets; document isolation headers, emitted files and memory limits. Gesture resume, setup failure, suspension, processor failure and async shutdown are explicit. No main-thread waits or ScriptProcessor fallback. |
| AU16 | Publish bounded numeric debug snapshots/trace with separate loss counters. Explain silence with state, independent causes, group/global limits, score contributions, spatial inputs and virtual age; correlate owner-side preparation/admission/policy failures without inventing voices. Owner queries expose accepted reservations and acquired terminals independently of stale renderer caches; terminal mailboxes are durable. Build a small audio gym, descriptor validators and a thin application adapter with explicit owned-loop teardown, bounded cooldown/duplicate suppression before admission and rollback on failure. Use existing terminal/HTML output. Reproducible fixtures include applied sample frames, assets and configuration; incomplete traces cannot claim exact replay. |
| AU17 | Deliver deterministic fixtures, failure injection, concurrency tests, allocation probes, listening/quality checks and measured callback workload evidence. Verify installed SDK/static closure and native/web regressions with pinned checks. Hardware/browser gates stay pending when unavailable. |

## Default planning limits

Defaults are engineering starting points, not performance results: 256 logical
resident slots, 64 total mixed voices including transitions and four protected
streams, 16 resident concurrency groups, 16 buses, 8 active modifiers, 512 queued
commands, 32 commands per batch, 64 applied commands per 128-frame control quantum,
and four stream instances with 32768 stereo frames each. Set capacities before startup; no
unbounded growth in rendering. The design specifies exact admission and bounds.

The sample rate is session configuration, normally 48000 Hz; browser output
uses the actual AudioContext rate. A device period request of 256 frames is a
hint, not a guaranteed device quantum or end-to-end latency.
