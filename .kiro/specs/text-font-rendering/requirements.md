# Text and font rendering requirements

Status: implementation specification; no implementation or validation is claimed.
Research and repository inspection: 2026-10-01, `origin/main` at `fa0fc7f`.

Read [design.md](design.md), [tasks.md](tasks.md), and the
[research](../../../docs/architecture/text-font-rendering-research.md) together.
The [handoff](../../../docs/architecture/text-font-rendering-kiro-handoff.md)
contains copyable Kiro prompts. The research includes the relevant findings from
the gitignored books; access to the PDFs is unnecessary.

## Intended result

Ludus applications can load packaged font bytes, shape UTF-8 text, measure it,
and render crisp screen-space text using the same engine API on Linux/Vulkan and
Emscripten/WebGPU. The first consumers are debug overlays and UI labels. Choose
HarfBuzz plus FreeType, a bounded R8 coverage atlas, and ordered instanced quads.
Do not substitute browser DOM/canvas text or an ASCII-only bitmap font.

This is a font renderer and a shaped-run API, not a complete paragraph/editor
system. A run has one font, physical pixel size, script, language, and explicit
horizontal direction. Applications compose runs/lines. Latin, Hangul, and an
Arabic RTL run are required fixtures. Automatic Unicode bidi paragraph analysis,
script itemization, font fallback, wrapping, caret navigation, selection, and IME
are explicitly deferred. Do not advertise those capabilities as implemented.

## Behavioral requirements

### T01 — Both production backends

The same CPU shaping/rasterization code and font assets shall build for native
Linux and wasm. Actual Vulkan and WebGPU pipelines shall draw the resulting text.
Adding only a browser renderer, a native clear, JS text drawing, or test doubles
does not satisfy this requirement. GPU-unavailable states shall preserve useful
headless font/layout operation and return a visible status.

### T02 — Reproducible dependencies and assets

Use FreeType 2.14.3 and HarfBuzz 14.5.1 as the initial common source pins; use the
configuration in the design. Resolve native dependencies through the existing
Conan bootstrap and web dependencies through the separate pinned web bootstrap.
Record verified source hashes, options, and font licenses before implementation
depends on them. Configure/build shall work offline after bootstrap. Do not use
the SDK's older HarfBuzz port alongside a different native version.

### T03 — Explicit ownership and lifetime

Font loading shall copy bytes into bounded owned storage. Faces, layouts, atlas
entries, and GPU resources shall have explicit owners and generation-checked
handles. Font removal with live layouts shall return `Busy`. Shutdown shall be
idempotent. Caller spans shall never be retained by a GPU/backend callback.

### T04 — UTF-8 and shaping

Validate UTF-8 strictly, including overlong encodings, invalid continuation
bytes, surrogates, and values above U+10FFFF. Report the first invalid byte.
Reject unsupported controls or strong scripts inconsistent with the run spec.
Shape through HarfBuzz's OpenType/FreeType integration, preserving glyph IDs,
signed advances/offsets, and original UTF-8 byte clusters. Preserve HarfBuzz output
order for RTL. Never equate a code point with a glyph, add separate kerning after
shaping, or reverse UTF-8 bytes. No implicit Unicode normalization is performed.

### T05 — Measurement

Expose baseline-relative ink bounds, advance, ascent, descent, and line advance
in physical pixels. Spaces may advance without producing a quad. Empty text
shall be a successful empty layout. Negative bearings, combining marks,
ligatures, and nonzero shaping offsets shall be accounted for. Measurement and
drawing shall use the same face size, load flags, and shaped positions.

### T06 — Missing glyphs and supported font formats

Support scalable monochrome-outline TTF/OTF, with an explicit collection face
index. Missing characters shall produce that face's `.notdef` glyph and a
missing-glyph diagnostic. Failure to rasterize `.notdef` shall be an explicit
failure. No automatic fallback search is implied. Variable-axis selection,
synthetic styles, bitmap-only fonts, color emoji/COLR/SVG, and WOFF are outside
this milestone; never interpret color pixels as R8 coverage.

### T07 — Correct raster data

Use grayscale coverage and light hinting at the requested integer physical pixel
height. Normalize FreeType bitmap row orientation and signed pitch. A grayscale
sample is coverage, not an sRGB color. Normalize non-256 gray ranges or reject
them explicitly. Validate sizes and strides before copying. Zero-area glyphs
retain metrics and need no atlas allocation. No LCD subpixel rendering is used.

### T08 — Bounded atlas

Cache by font generation, glyph ID, physical pixel height, and raster mode.
Use append-only, nonrotated rectangles, transparent gutters, no mipmaps, and
fixed page/entry limits. Never move or overwrite an existing glyph in a live
generation. Overflow shall return `ResourceLimit`; it shall never silently drop
characters, continuously grow memory, or substitute a missing-glyph marker for
an atlas failure. Reset and device recovery shall invalidate prepared GPU state.

### T09 — Separate preparation and drawing

Shaping and cold glyph preparation are explicit operations allowed to allocate
and fail. A warmed frame using retained layouts shall do no shaping,
rasterization, CPU heap allocation, page allocation, or synchronous GPU readback.
Upload dirty rectangles once before rendering; drawing shall only resolve cached
entries and append bounded instances. An unprepared glyph shall return
`NeedsPreparation`, not start rasterization inside a render pass.

### T10 — Ordered batching and clipping

One visible glyph shall produce one instance of six generated quad vertices.
Batch only adjacent instances with compatible atlas page, pipeline, and scissor.
Preserve submission/painter order, including overlapping translucent text.
Clipping uses validated half-open physical-pixel rectangles intersected with the
current framebuffer. Empty/outside clips and zero-area glyphs produce no draw.

### T11 — Pixel coordinates and scale

The text API shall use framebuffer pixels with a top-left origin and a baseline
position; its font height is physical pixels. Applications convert CSS/logical
coordinates and font size using actual content scale/DPR. A scale change requires
reshaping at the new raster size, not magnifying old bitmap glyphs. Native extent,
browser backing-store size, and glyph placement shall agree after resize.

### T12 — Coverage, color, and blending

Use linear straight RGB plus opacity at the text API, premultiply after sampling
coverage, and use source-one/destination-one-minus-source-alpha blending. Blend
into an sRGB attachment view so blending occurs in linear space. Keep the initial
presentation surface opaque and SDR. Pipeline format shall match the actual
attachment view on each backend. Verify dark/light backgrounds, partial opacity,
and colored overlapping glyphs; an opaque white-on-black screenshot is inadequate.

### T13 — GPU and browser lifecycle

Resource initialization shall follow RHI readiness and expose
`Pending/Ready/Failed/DeviceLost` without a browser blocking loop. Late callbacks
shall be checked against a numeric device/renderer generation and release their
results safely. Instance/staging memory and textures shall remain valid until GPU
retirement. Resize, hidden/zero-size frames, shutdown, failed initialization, and
stop/restart shall not leak or use stale native/WebGPU handles.

### T14 — Errors, budgets, and observability

All public fallible calls shall use explicit statuses and `noexcept`. Validate
handles, enums, finite coordinates/colors, multiplication/addition, glyph counts,
UTF-8 lengths, image limits, and upload sizes before indexing or GPU calls.
Failures shall leave documented reusable state and never expose partial output
as success. Expose bounded counters and glyph/atlas inspection sufficient to
diagnose shaping, caching, uploads, clipping, and submission. Use existing
logging/profiling without requiring a text renderer to report its own failure.

### T15 — Repository and SDK boundaries

Follow `AGENTS.md`, `.kiro/steering/`, and applicable ADRs, especially 0001,
0003–0009. No engine exceptions, raw primitive spellings, heavy public headers,
private-header exports, or upward Foundation dependencies. Keep third-party
font/GPU types private. Export CPU Text and GraphicsText targets correctly,
including the link closure of their static dependencies. Verify a consumer that
has neither the engine source tree nor access to private headers.

### T16 — Completion evidence

Provide native and wasm production-path tests, failure injection, a shared
visual demo, measured warm/cold behavior, and the validation matrix in tasks.md.
Require actual rendered-output verification on Linux/Vulkan and a supported
browser/WebGPU. Mocked C-API counts, Node wasm execution, and successful shader
compilation are useful but cannot replace either rendering gate. Report missing
hardware/compositor checks as pending; leave affected milestones incomplete.

## Scope limits

No UI framework, layout engine, general-purpose render graph/RHI rewrite, new
allocator, event bus, ECS, job worker, runtime shader compiler, system font
discovery, mandatory network font service, rich text, outlines/shadows,
world-space text, arbitrary scale/rotation, MSDF, or analytic outline renderer
is required. Keep replaceable private raster/atlas/backend boundaries for later
measured extensions. Text rendering shall not change the keyboard-only input
contract or claim to implement text input.
