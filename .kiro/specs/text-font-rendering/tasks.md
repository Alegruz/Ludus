# Text/font rendering implementation milestones

Status: all implementation tasks unchecked. This file is the execution/evidence
ledger, not evidence that code or GPU validation already exists.

Read [requirements.md](requirements.md), [design.md](design.md), and
[research](../../../docs/architecture/text-font-rendering-research.md) before
starting. Implement F0–F6 in order, with small reviewable changes. Linux and web
are coequal targets in each applicable milestone; do not implement Linux first
and defer the entire web port to an unspecified later task.

## F0 — Baseline, dependencies, assets, and build seams

Requirements: T01, T02, T15.

- [ ] Inspect current RHI/frame/smoke/tooling code and record the actual starting
  commit. Preserve merged keyboard and WebGPU work. Record any existing test
  failures before making changes.
- [ ] Implement native Conan and web source-bootstrap paths for the selected
  FreeType/HarfBuzz pins/options. Record verified URLs, hashes, licenses, effective
  options, and target architecture. Ensure the old SDK font ports are not linked.
- [ ] Pin/bootstrap the host glslang tool and its build dependencies; add a small
  reproducible shader generation seam. No runtime shader compiler.
- [ ] Enable C carefully and add Text/GraphicsText targets and public-header file
  sets. Keep vendored checks separate from Ludus format/tidy/warnings gates.
- [ ] Add fixed licensed Latin/Hangul/Arabic fixture font assets with hashes and
  browser packaging. Test asset loading without system fonts or runtime network.
- [ ] Build/link tiny native and wasm production dependency probes, including
  HarfBuzz's FreeType integration. Verify versions/configuration at runtime or in
  a generated build manifest. Verify offline reconfigure/rebuild after bootstrap.
- [ ] Record sRGB attachment support/integration prerequisites for both backends;
  implementation is F3, not an architecture-selection question left to Kiro.

Gate: pinned dependencies build/link on both toolchains with engine exceptions
disabled, and the installed/exported link strategy is concrete. Do not claim
font rendering or native/WebGPU pixel validation at this milestone.

## F1 — Owned fonts, shaping, metrics, and headless tests

Requirements: T03–T06, T11, T14, T15.

- [ ] Implement FontSystem, validated generation handles, owned font bytes,
  bounded layouts/source bytes, reference/lifetime rules, and fallible cleanup.
- [ ] Implement strict UTF-8/control/script validation and explicit run properties.
  Implement hb-ft size/load-state synchronization and shaped glyph/byte-cluster
  records without extra kerning or manual RTL reversal.
- [ ] Implement measurements, empty/space behavior, `.notdef` diagnostics, and
  unsupported-font/status handling. Define FT/HB error translation.
- [ ] Test `AV`, `office`, precomposed/combining accents, Hangul, Arabic joining and
  marks in RTL, negative bearings, spaces, empty input, missing glyphs, malformed
  UTF-8, mismatched script, unsupported controls, and run/glyph/font limits.
- [ ] Test source-byte lifetime, font removal while layouts exist, alternating
  sizes, stale/invalid/wrapping handles, partial creation, and allocation failures.
- [ ] Run the same production CPU implementation in a wasm font probe. Compare
  shape/metric fixtures and font hashes/options with native output; document
  tolerances. A JS reimplementation is not a test of the engine's shaping path.

Gate: complete headless production-path CPU behavior on native and wasm, with
bounded ownership/status failures and matching shaping evidence. GPU work remains
outstanding.

## F2 — Grayscale rasterization and bounded atlas

Requirements: T07–T09, T14.

- [ ] Implement outline light-hinted grayscale rasterization and borrowed scratch
  lifetime. Normalize signed pitch, non-256 gray ranges, and top-down rows.
- [ ] Implement deterministic shelf placement, zero gutters, no-image glyphs,
  fixed lookup/metadata capacity, complete keys, and append-only CPU shadow pages.
- [ ] Implement explicit preparation, dirty-region coalescing, visible capacity
  failure, retry semantics, reset generation, and reproducible atlas dumps.
- [ ] Test positive/negative pitch with synthetic fixtures, width/stride mismatch,
  zero-area glyphs, negative bearings, unsupported pixel modes, oversized glyphs,
  edge/gutter fit, hash collisions, identical glyphs at different sizes/fonts,
  duplicate preparation, full page/entry tables, and checked arithmetic.
- [ ] Verify atlas reset invalidates preparation while retained layouts remain
  reusable. Verify failed preparation cannot publish a partial ready layout.
- [ ] Run real FT raster/atlas fixtures in native and wasm probes and compare
  normalized coverage bytes/placement. Include engine allocation-failure cases.

Gate: deterministic bounded CPU atlas on both builds; inspect dumps for clipping,
missing rows, flipped rows, and gutter contamination. No GPU result is claimed.

## F3 — Real Vulkan and WebGPU coverage-quad pipelines

Requirements: T01, T10, T12, T13, T15.

- [ ] Add the narrow public RHI coverage interface and current frame info. Keep
  font and private GPU types out of it. Validate the 48-byte instance ABI.
- [ ] Implement R8 page creation, staged updates, instance upload, scissor batching,
  shader projection, premultiplied blend, and status/lifetime rules on **both**
  backends. Uploads/copies occur before a render pass begins.
- [ ] Add Vulkan attachment views/render-pass recording and the compiled shader
  pipeline to actual acquired frames. Handle transfer/read barriers, descriptors,
  mapped-memory flushing, per-frame fences, failed acquisition/submission, and
  deferred resource release. Preserve clear/presentation behavior.
- [ ] Query native supported surface formats and use a supported sRGB attachment.
  Configure WebGPU's preferred unorm base format plus compatible sRGB attachment
  view; pipeline/view formats must match. Adjust existing smoke format use safely.
- [ ] Add WebGPU generation-checked async setup/error/loss cleanup and valid padded
  copy layouts where applicable. Preserve browser event-loop progress.
- [ ] Test opaque synthetic masks before involving fonts: exact corners, UV
  orientation/texel centers, partial scissors, alpha edges, linear RGB, overlapping
  colors, alternating atlas pages, odd row widths, and upload limits.
- [ ] Test staged ownership, skipped frames, in-flight buffer reuse/destruction,
  resize/format recreation, failed pipeline/page creation, shutdown/restart, and
  late callbacks. Assert zero stale GPU operations through the existing test seams.
- [ ] Obtain actual rendered-output evidence for the synthetic quad tests on
  Linux/Vulkan and supported WebGPU. Record validation messages and images.

Gate: actual pixels on both production backends and safe lifecycle evidence.
Without hardware/compositor evidence, leave the rendering gate unchecked even if
all mocks/probes pass. Do not replace the native draw with a clear-only success.

## F4 — Text integration, shared demo, and scale/recovery

Requirements: T01, T08–T13, T16.

- [ ] Implement renderer readiness, prepared-layout generation records, bounded
  upload progress, per-frame lists, atomic AddLayout, ordered batches, and Submit.
  Warm drawing must not invoke HB/FT, allocate, or perform readback.
- [ ] Add the same engine text demo on native/web, preferably extending compatible
  shared smoke infrastructure. Use packaged fonts and explicit Latin/Hangul/Arabic
  runs; demonstrate metrics/baselines, sizes, clipping, missing glyphs, and color.
- [ ] Implement actual pixel-size conversion from app logical coordinates/DPR,
  rereshaping on scale change, and correct extent updates. State native scale
  limitations honestly. Preserve existing keyboard and triangle/smoke behavior.
- [ ] Exercise start/loading/failure status, zero-size/hidden frames, resize,
  stop/restart, device loss, CPU-shadow reupload, atlas reset/reprepare, and full
  atlas handling without leaks or a blocking loop. Recovery may span frames.
- [ ] Compare native/wasm shaped data and real GPU images against a deterministic
  CPU reference using the same coverage masks and sRGB compositing rules. Capture
  light/dark backgrounds, opacity, overlap, and DPR 1/1.5/2.

Gate: the same public text API draws real text on Linux and web, with scale and
recovery evidence. A browser-only screenshot does not satisfy the native gate.

## F5 — Debugging, measurements, and focused optimization

Requirements: T09, T14, T16.

- [ ] Add bounded counters and explicit inspection/dump tools for clusters,
  glyph positions, raster bytes, atlas/generation, uploads, batches, and scissors.
  Integrate existing logging/profiling without rendering/logging recursion.
- [ ] Benchmark warm retained labels and changing/cold text separately, at 1k/10k
  visible glyphs. Record median/p95 CPU time, GPU time when available, allocations,
  raster/miss counts, uploads, batches, retained memory, and web artifact size.
- [ ] Demonstrate repeated warmed draws have zero engine heap allocations, zero
  HB/FT calls, zero page allocations, and zero dirty glyph uploads. GPU queue
  submission/framework overhead must be labeled separately from engine metrics.
- [ ] Verify draw calls correspond to adjacent page/scissor runs; diagnose avoidable
  state changes without sorting away alpha order. Measure shelf utilization.
- [ ] Fix measured issues within the selected design. Do not add MSDF, analytic
  curves, eviction, compressed instances, workers, or a general renderer on an
  unsupported claim that they must be faster.

Gate: repeatable measurements and usable debug artifacts on both builds. No
universal FPS claim or invented timing target is required.

## F6 — Full validation and SDK handoff

Requirements: all T01–T16.

- [ ] Run the validation matrix below on the final implementation and record exact
  commands, versions, hashes, pass/fail/skip/pending results, and artifact paths.
- [ ] Verify native SDK install/consumer and a focused wasm install/export consumer
  using Text/GraphicsText with no engine private headers/source paths. The current
  `scripts/install-sdk` web path is unsupported; add/use a focused documented
  `cmake --install` plus Emscripten consumer probe rather than claim it already works.
  Check static dependency link closure and font/shader asset packaging explicitly.
- [ ] Complete Linux/Vulkan and real-browser/WebGPU output/lifecycle verification,
  with no unexpected graphics validation errors. Preserve evidence in the repo.
- [ ] Update API usage/build/debug documentation and the milestone evidence ledger.
  Name supported fonts/scripts/run limitations and unimplemented paragraph features.
- [ ] Recheck the full diff against AGENTS/steering, warning/exception/include rules,
  and unrelated input/WebGPU regressions. Never disable gates merely to pass.

Gate: all mandatory checks passed. If an environment cannot provide a real GPU
or compositor, deliver the implemented code and clearly mark the corresponding
gate pending; do not check F6 or report the system fully validated.

## Validation matrix

Use current pinned workflows. These are required implementation checks, not
commands that were run when authoring these documentation files.

| Area | Required evidence |
| --- | --- |
| Native debug | `./scripts/build linux-clang-debug`; `./scripts/test linux-clang-debug` |
| Native development/static analysis | `./scripts/build linux-clang-development`; `./scripts/check linux-clang-development --all`; warnings as errors |
| Native sanitizers | `./scripts/build linux-clang-asan-ubsan`; `./scripts/test linux-clang-asan-ubsan` |
| Headers/build budget | Existing self-sufficiency/foundational-include gates; `./scripts/check-build-budget`; no raised budget without measurements |
| Native headless | CPU text tests with Wayland disabled; GPU operations report unavailable status without breaking CPU Text |
| Native presentation | Real Wayland/Vulkan demo and image/reference comparison, resize, frame failure, reset/restart; validation output recorded |
| Browser development | `./scripts/build web-emscripten-development`; `./scripts/test web-emscripten-development`; `./scripts/check web-emscripten-development --all` |
| Browser release | `./scripts/build web-emscripten-release`; `./scripts/test web-emscripten-release`; artifact size |
| Browser CPU tests | New wasm production font/atlas fixtures, plus existing Foundation/Platform/RHI/frame probes |
| Browser presentation | Secure-context server or localhost, supported WebGPU with ordinary settings, real glyph images, DPR/resize/hidden/restart/loss checks; browser/adapter/version recorded |
| Native SDK | `./scripts/install-sdk linux-clang-development`, expanded consumer linking both text targets |
| Web exports | Focused Emscripten install/export consumer; no native Conan/Volk/Wayland or private includes; full static font link closure |
| Regressions | Existing keyboard tests/demo, shared smoke simulation, triangle and frame/probe lifecycle behavior |
| Performance/failure | Warm/cold F5 records, allocation failures, atlas/full-frame limits, async/device failure injection |

Catch2 exceptions remain confined to test executables. Wasm probes test compiled
production code. Mocked lifecycle tests complement real rendered output, and
skipped hardware tests are reported independently from passing CPU tests.

## Evidence ledger template

Update this table as work actually completes; add links to checked-in reports.
Store reports/images in `docs/architecture/text-font-rendering-evidence/` (create
that directory during implementation). No machine-specific absolute source paths
or raw copyrighted book pages belong in the implementation package.

| Milestone | Implementation commit | Native results | Web results | Actual GPU evidence / pending gates |
| --- | --- | --- | --- | --- |
| F0 | pending | pending | pending | not applicable |
| F1 | pending | pending | pending | not applicable |
| F2 | pending | pending | pending | not applicable |
| F3 | pending | pending | pending | pending on both |
| F4 | pending | pending | pending | pending on both |
| F5 | pending | pending | pending | measurements pending |
| F6 | pending | pending | pending | pending on both |
