# Low-level renderer systems: Gems reference review

Status: Design evidence, October 7, 2026; no renderer implementation or benchmark.
The [architecture](renderer-systems.md) was saved before searching the article map.
This review records the subsequent source-driven changes. Detailed device,
camera, resource and text contracts remain in their existing owners.

## Selection and evidence

Searched the local `references/game-dev-gems-toc.md` for renderer architecture,
submission, viewport, materials/shaders, culling, lighting, shadows, atlases and
fonts. Selected six articles for actual text inspection. Read the relevant
sections listed below, including limitations and tradeoffs; this is not a claim
to have read every chapter/book or inspected companion code. The atlas and font
chapters were inspected through their conclusions/references. Some source text
has extraction artifacts; author/title/page details and the key diagrams were
cross-checked against rendered PDF pages.

PDF positions are one-based file pages in the user's local copies. Those ignored
PDFs and extraction/rendering scratch files are not distributed in the repository.
This record and the architecture are usable without them. Titles are discovery
evidence until actual article text is inspected; unrelated catalog entries did
not alter the design merely because their names sounded relevant.

| Source inspected | Printed pages and sections | Local PDF pages |
| --- | --- | --- |
| Donald Revie, “Designing a Data-Driven Renderer,” *GPU Pro 3* (2012), V.4 | pp. 291–318; 4.2–4.8, especially representation/context, frame graph and case-study tradeoffs | 295–322 |
| Markus Billeter, Ola Olsson and Ulf Assarsson, “Tiled Forward Shading,” *GPU Pro 4* (2013), II.4 | pp. 99–114; 4.4–4.8, transparency, MSAA and clustered comparison | 116–131 |
| Takahiro Harada, Jay McKee and Jason C. Yang, “Forward+: A Step Toward Film-Style Shading in Real Time,” *GPU Pro 4* (2013), II.5 | pp. 115–134; 5.2–5.6, light-list construction, culling data and shadow cost | 132–151 |
| Arseny Kapoulkine, “Writing an Efficient Vulkan Renderer,” *GPU Zen 2* (2019), IV.4 | pp. 215–247; revisited 4.4–4.6, ordered dependencies, attachments and pipelines | 227–259; pertinent sections 250–257 |
| Manny Ko, “A Fast and High-Quality Texture Atlasing Algorithm,” *Game Engine Gems 3* (2016), chapter 9 | pp. 111–120; packing decomposition, borders and topology-aware filtering | 106–115 |
| Aurelio Reis, “Fast Font Rendering with Instancing,” *Game Programming Gems 8* (2010), 1.1 | pp. 3–11; quad data, batching, instancing limits and painter order | 18–26 |

Visual inspection covered *GPU Pro 3* Figure 4.23 (printed p. 313), *GPU Pro 4*
Figure 4.3 (p. 108), and *Game Engine Gems 3* Figure 9.1 (p. 113). The first
is a context/control traversal, not a modern resource-hazard graph; the second
shows why transparent bounds differ; the third demonstrates border reservation.

## Findings and adopted changes

### Representation, context and technique boundaries

Revie separates shared representation from camera/viewport/target context and
describes selectable techniques and ordered passes. The case study also reports
configuration growth, difficult special cases and optimization costs.

**Change:** Make pass inputs complete immutable records, declare depth/shadow/color
variants in the material package, resolve parameter mappings before drawing and
bound configurations. Keep stable views separate from cameras/targets.

**Departure:** No XML object pipeline, global context push/pop, entity requirement
for postprocess/UI, name-searching draw path or universal emulation of unavailable
hardware. Concrete functions and typed data make the useful separation easier
to follow under Ludus's explicit ownership rules.

### Transparent lighting and view-dependent culling

Billeter, Olsson and Assarsson distinguish opaque and transparent depth bounds,
retain ordinary alpha sorting, and discuss both MSAA samples and the additional
constant costs of clustering. Tiled and clustered methods trade workload costs;
the chapter does not establish a universal winner.

**Change:** Begin clustered assignment with conservative view-space cells that
cover transparent geometry. Keep opaque depth/normal pruning separate. Define
the finite lighting range, empty-depth behavior and multisample handling.

**Departure:** No direct transplantation of GLSL buffer-texture layouts, fixed
tile sizes or historical performance numbers. The CPU light path remains useful;
clustered assignment does not solve intersecting transparency order.

### Light-list capacity and shadow cost

Harada, McKee and Yang separate compact culling bounds from lighting properties,
generate lists through local then global storage, discuss depth occupancy masks,
and show that many-light shading does not remove shadow-generation costs.

**Change:** Separate culling/shading arrays, use capped fixed-stride lists first,
validate every append and published count, visualize overflow and independently
budget shadow assignments. A saturated list takes a stable bounded fallback.

**Departure:** Their example increments an attempted append count while guarding
the local store; its subsequent export illustrates why a production design must
bound the exported count as well. Ludus does not assume the demo's capacity
covers all scenes. The 2.5D masks and ray-cast shadow extension remain candidates,
not baseline features or proof of contemporary ray-tracing performance.

### Ordered work, attachments and pipelines

Kapoulkine explains ordered dependency planning, load/store/resolve bandwidth,
pipeline creation/cache costs and ahead-of-time enumeration of material states.
The existing [RHI/GDI review](rhi-gdi-gems-review.md) remains the detailed owner
of allocation, descriptor and command lifetime findings from this source.

**Change:** Make material state enumeration an admission/prewarm requirement,
preserve explicit attachment intent and keep streaming scheduling outside the
scene-pass traversal while importing its readiness dependencies.

**Departure:** No Vulkan-specific graph in public Renderer, no promise that a
driver cache eliminates first-use stalls, no forced newest Vulkan feature set,
and no assumption that tile and immediate GPUs have identical fast paths.

### Atlas borders and chart semantics

Ko decomposes chart packing into placement, fit and quality; reserves borders
before packing; and filters chart seams using original mesh topology. He also
identifies cases where simple greedy packing leaves quality on the table.

**Change:** Give glyph, sprite and lightmap atlases separate contracts. Keep
runtime glyph shelves simple, use offline chart packing for lightmaps, and budget
filtering footprints before placement. Preserve chart adjacency for lightmap
filtering and validate mip-safe gutters or a declared LOD clamp.

**Departure:** No topology-aware diffusion between independent letters or sprites,
no live atlas relocation, and no general annealing/bitmask packer until offline
occupancy measurements justify it. Better packing does not itself make mips safe.

### Glyph submission and historical instancing costs

Reis separates fixed quad corners from per-glyph attributes, describes compatible
batch boundaries and painter order, and notes that the tested instancing methods
have hardware-dependent overhead and constant limits.

**Change:** Retain compact glyph instances, completion-scoped uploads, adjacent-only
batching and ordering. Add tiny-label as well as large-overlay benchmarks.

**Departure:** No D3D9 discard path, Shader Model 3 constant-array packing, fixed
85-quad limit or inference that modern instancing is always faster. Unicode
shaping remains with Text; the chapter is submission evidence, not a paragraph
layout or font-quality specification.

## Changes to the initial design

| Initial decision | Refinement now in the architecture | Gate added |
| --- | --- | --- |
| Explicit passes and material families | Complete pass inputs and cooked depth/shadow/color variants | Variant count and reflected layout validation |
| Conservative clustered lists | View-space range covering transparent geometry; separate opaque pruning | Glass in front of opaque geometry, sky and MSAA cases |
| Bounded lighting | Written-count safety, compact culling arrays, independent shadow budget | Local/global overflow and stable fallback fixtures |
| Texture semantics and atlases | Distinct atlas owners and mip/filter footprints reserved before packing | Lightmap seams, sprite bleed and glyph gutters |
| Instanced glyphs | Small-label and batch-break comparisons, preserved painter order | Pixel parity and upload-byte/CPU/GPU measurements |
| Pipeline prewarming | Enumerate legal material states; expose cold misses | First-use/reload/driver-cache failure behavior |

## Other articles and current research

The catalog also contains Cozzi's “A Framework for GLSL Engine Uniforms” and
“Delaying OpenGL Calls,” Schertenleib's “A Multithreaded 3D Renderer,” and Moore's
“A GPU-Managed Memory Pool.” This task reused their existing
[RHI/GDI review](rhi-gdi-gems-review.md) rather than claim fresh readings.
Camera chapters are covered by the [camera review](camera-systems-gems-review.md);
font alternatives by the [Text research](text-font-rendering-research.md).

Perspective/cached shadow mapping, deferred shading, SSAO and many other mapped
articles remain targeted follow-up readings when an implementation needs the
specific technique. They did not justify replacing the core design. Local PDF
availability is not a reason to accumulate features without a consumer.

The publisher's abstract for Olsson, Billeter and Assarsson's
[“Clustered Deferred and Forward Shading”](https://diglib7.eg.org/items/6342d4d6-5220-4376-a5c6-a153058f4a3c/full)
(*High Performance Graphics*, 2012, pp. 87–96) corroborates grouping by 3D position
and optional normals. The full preprint fetch was unavailable; the actual local
tiled chapter supplies the more detailed lighting evidence used here.

Current API checks used Khronos's
[pipeline cache guide](https://docs.vulkan.org/guide/latest/pipeline_cache.html),
[pipeline management sample](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_cache/README.html),
[render-pass specification](https://docs.vulkan.org/spec/latest/chapters/renderpass.html)
and [synchronization specification](https://docs.vulkan.org/spec/latest/chapters/synchronization.html).
The [WebGPU specification](https://www.w3.org/TR/webgpu/) search excerpt verified
the optional timestamp feature; full-page retrieval exceeded the browsing limit.
Existing RHI source/architecture provides the pinned browser constraints.

Google Filament's
[standard BRDF discussion](https://github.com/google/filament/blob/main/docs/Filament.md.html)
provides a contemporary physically based shading reference. The
[SIGGRAPH 2025 Advances course program](https://advances.realtimerendering.com/s2025/)
was inspected as a discovery source for stochastic lights, ray tracing and
transparency. Program descriptions are not algorithm readings or Ludus benchmarks.
Those paths need their own reviews, capabilities and measurements before adoption.

Thanks to all named authors. The architecture adopts ideas and records departures;
it copies no implementation code, diagrams or companion assets. Implementation
must carry concise references near the affected code, preserve applicable notices
and link this decision record. No book technique or current feature is a measured
performance claim for Ludus until its stated acceptance gate passes.
