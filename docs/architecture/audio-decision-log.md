# Ludus audio decision log

Status: living document, started 2026-10-02 during A0 implementation of the
[audio spec](../../.kiro/specs/audio/README.md) (see
[requirements](../../.kiro/specs/audio/requirements.md),
[design](../../.kiro/specs/audio/design.md),
[tasks](../../.kiro/specs/audio/tasks.md),
[research](audio-research.md)).

This log records decisions actually made while implementing A0-A7, with the
evidence behind them. It never fabricates a measurement, hash or hardware
result. "Pending" means not yet run in this environment, not "assumed passing".

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
