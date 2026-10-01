# Kiro handoff: build text and font rendering

The implementation package is self-contained:

- `.kiro/specs/text-font-rendering/requirements.md`
- `.kiro/specs/text-font-rendering/design.md`
- `.kiro/specs/text-font-rendering/tasks.md`
- `docs/architecture/text-font-rendering-research.md`
- `docs/architecture/text-font-rendering-kiro-handoff.md`

It selects HarfBuzz shaping, FreeType grayscale masks, a bounded R8 atlas, and
ordered instanced quads on Linux/Vulkan and Emscripten/WebGPU. It includes the
local Gems readings, current alternatives, repository prerequisites, contracts,
budgets, failure/lifetime rules, and verification gates. Kiro needs none of the
gitignored PDFs. Implementation, dependency integration, measurements, and all
GPU gates are outstanding.

## Prompt 1 — Implement the complete sequence

Use this after the five files are available in Kiro's checkout:

```text
Implement Ludus's text/font rendering system from the committed specification.
Read AGENTS.md and applicable .kiro/steering/ instructions first, then:
- .kiro/specs/text-font-rendering/requirements.md
- .kiro/specs/text-font-rendering/design.md
- .kiro/specs/text-font-rendering/tasks.md
- docs/architecture/text-font-rendering-research.md

Execute F0 through F6 in order, in small reviewable changes, and update the task
and evidence ledger as work actually passes. This authorizes implementation;
do not stop after making another design or seek approval between ordinary stages.
Revalidate the current checkout. Preserve merged keyboard input, WebGPU work,
existing smoke/probes, unrelated changes, and other specifications.

Implement the selected architecture: CPU Text owns font bytes/faces and immutable
UTF-8 shaped runs/metrics through common pinned HarfBuzz/FreeType sources;
GraphicsText owns explicit glyph preparation, bounded append-only R8 pages, and
painter-ordered instance lists; RHI owns the small public coverage-quad resource/
upload/draw interface and real private Vulkan/WebGPU implementations. Keep each
module and installed header boundary correct. Do not expose private probe handles.

Support the same engine API, font assets, CPU behavior, and actual GPU text on
Linux and web. The native smoke path currently only clears; build its real text
pipeline too. Shape explicit horizontal single-font/script/direction runs,
including Latin, Hangul, and Arabic RTL fixtures. Do not claim automatic bidi
paragraphs, fallback, wrapping, editing, or IME. Font rendering is separate from
keyboard/text input.

Follow the design's source pins/options and bootstrap/offline rules. Avoid the
SDK's mismatched old font ports. Verify real source/font hashes and licenses;
never fabricate them. Handle UTF-8 byte clusters, hb-ft size/load-state changes,
negative bearings, combining marks, signed bitmap pitch, empty/space glyphs,
missing glyphs, cache keys, gutters/UV texel centers, bounds/overflow, and explicit
capacity failure. Keep shaping/rasterization out of warmed drawing.

Use physical framebuffer coordinates and rerasterize after size/DPR changes.
Implement matching sRGB attachment views/pipeline formats, linear coverage,
premultiplied blending, scissors, adjacent-only batching, pre-pass uploads, and
GPU retirement. Preserve browser event-loop progress and generation-check late
callbacks. Cover resize, skipped frames, atlas reset, shutdown/restart, loss and
CPU-shadow reupload without stale handles or in-flight buffer reuse.

Follow C++23, Ludus aliases, no engine exceptions, noexcept/status errors,
fallible existing storage, lightweight public headers, warning/tidy/format and
build-budget gates. Do not build a new allocator, UI framework, general render
graph, system-font service, worker, runtime shader compiler, MSDF renderer,
analytic curve renderer, or eviction scheme for this milestone.

The PDFs are intentionally unavailable. Use the committed findings and primary
source links; do not ask for PDFs or historical sample code. Record justified
integration corrections with evidence; do not delegate architecture selection
back to me or weaken requirements just to pass checks.

Finish with the shared demo, native/wasm production-path tests, failure injection,
warm/cold measurements, header/build-budget checks, native SDK and wasm export
consumers, and the full pinned validation matrix in tasks.md. Actual rendered
output on Linux/Vulkan and real browser/WebGPU is mandatory. Mocks, Node wasm,
and shader compilation cannot replace either GPU gate. If hardware/compositor
access is unavailable, finish independent work, report the pending gate, and
leave affected tasks unchecked. Return changed paths, actual API/build usage,
verification artifacts, and precise supported/remaining capabilities.
Do not push, merge, or deploy unless separately instructed.
```

## Prompt 2 — One milestone at a time

Use this instead of Prompt 1 when controlling change size. Start with F0, then
replace `F0` with the next milestone only after reading its results:

```text
Implement milestone F0 of Ludus's committed text/font-rendering specification.
Read AGENTS.md, applicable .kiro/steering/, all three files under
.kiro/specs/text-font-rendering/, and
docs/architecture/text-font-rendering-research.md first. Treat requirements and
design as the concrete contract; the research already summarizes the local PDFs.
Preserve existing keyboard/WebGPU work and unrelated changes.

Implement this milestone's production code and meaningful tests for Linux and
web. Complete its required checks and update the evidence ledger. Do not stop at
a plan, change the selected architecture without technical evidence, claim GPU
verification from mocks, or check a gate that is pending. Report changed paths,
commands/results, API usage, pending gates, and readiness for the next milestone.
Do not advance to a different milestone, push, merge, or deploy in this turn.
```

## Prompt 3 — Review the finished implementation

```text
Audit Ludus's implemented text/font renderer against T01-T16 and F0-F6 in
.kiro/specs/text-font-rendering/. Read the design, research and actual current
code; a checked checkbox is not proof. Follow AGENTS.md and steering rules.

Verify native and wasm dependency/options/font parity; strict UTF-8 and hb-ft
state; shaped positions/clusters/RTL; normalized coverage/bearings; complete
cache keys and bounded atlas/upload/frame limits; correct UVs, clipping, painter
order, sRGB attachment formats and premultiplied blending; warm allocations;
in-flight resources, async callbacks and device recovery; and SDK private-header
and static-link closure. Verify real Linux/Vulkan and browser/WebGPU images and
regressions in keyboard/smoke/frame behavior.

Fix concrete violations within scope, run appropriate pinned checks, and update
the evidence ledger honestly. Report actionable findings and supported limits.
Keep unavailable real-GPU checks pending; do not substitute mocks or invent
benchmark results. Do not push, merge, or deploy unless separately instructed.
```
