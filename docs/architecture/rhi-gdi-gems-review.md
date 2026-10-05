# RHI and Graphics Device Interface Reference Review

**Status:** Design evidence, 2026-10-04. The initial
[RHI and GDI architecture](rhi-gdi.md) was saved before opening
[the article map](../../references/game-dev-gems-toc.md). This review selected
relevant articles, read their actual text, cross-checked modern API constraints,
and updated the architecture. It did not implement or benchmark a renderer.

The durable contribution of these references is explicit ownership and data
flow, with optimizations selected by workload. The final Ludus proposal retains
a narrow device interface, an ordered pass compiler, and portable raster
variants, while improving allocation, staging, binding, and pipeline contracts.

## Reading scope and evidence

The map was searched for rendering architecture, resource/shader management,
command recording, GPU memory, synchronization, abstraction, and batching.
Five complete chapters were read from local PDF text. Selected opening pages,
attachment guidance, staging lifetime diagram, and uniform framework diagram
were also rendered and inspected. No companion code/CD was inspected. Other
mapped titles remain candidates, not evidence from unread articles.

PDF positions below are **one-based file pages** in this repository's reference
copies, distinct from the books' printed page numbers. These ignored local PDFs
require the user's reference collection; the tracked review remains usable as
a decision record without them.

| Source actually read | Printed pages | Local PDF pages | Useful architectural evidence |
| --- | --- | --- | --- |
| Sebastien Schertenleib, Game Engine Gems 1 chapter 9, [A Multithreaded 3D Renderer](../../references/Game%20Engine%20Gems%201.pdf#page=213) | 185-195 | 213-223 | Batch data locality, parallel recording granularity, synchronization, buffering latency |
| Jeremy Moore, Game Engine Gems 1 chapter 11, [A GPU-Managed Memory Pool](../../references/Game%20Engine%20Gems%201.pdf#page=247) | 219-231 | 247-259 | Streaming staging ring, fence-based reuse, application eviction policy, bounded relocation work |
| Patrick Cozzi, Game Engine Gems 2 chapter 5, [Delaying OpenGL Calls](../../references/Game%20Engine%20Gems%202.pdf#page=87) | 71-81 | 87-97 | Private selector state, dirty updates, avoiding redundant calls, early bulk transfers |
| Patrick Cozzi, Game Engine Gems 2 chapter 6, [A Framework for GLSL Engine Uniforms](../../references/Game%20Engine%20Gems%202.pdf#page=99) | 83-92 | 99-108 | Explicit scene/object state, uniform meaning separated from API state, cached derived values |
| Arseny Kapoulkine, GPU Zen 2, 3D Engine Design article 4, [Writing an Efficient Vulkan Renderer](../../references/GPU%20Zen%202%20Advanced%20Rendering%20Techniques.pdf#page=227) | 215-247 | 227-259 | Allocation, descriptors, commands, barriers, render graphs, attachment bandwidth, pipeline enumeration/prewarming |

## Vulkan allocation and resource bindings

Kapoulkine compares allocation strategies and binding models, emphasizing their
hardware and maintenance tradeoffs. He discusses suballocation, dedicated
requirements, persistent host mapping, VMA, descriptor pools, dynamic uniform
offsets, grouping by update frequency, and the cost of binding changes. Bindless
can reduce CPU submission work but requires suitable limits and increases shader
indirection and renderer complexity. It is not uniformly optimal.

**Ludus refinement:** Select a private pinned VMA adapter for general Vulkan
allocation. Query memory requirements, flush/invalidate rules and actual budgets;
do not inherit historical heap sizes or vendor-specific assumptions. Make
View/Material/Draw grouping a useful convention, use immutable binding snapshots,
and allocate small dynamic data from aligned rings. Native descriptor pools retire
by completion; persistent material sets have their own ownership. Portable
materials remain functional without bindless.

**Excluded:** A public native heap/descriptor API, fixed desktop binding counts,
per-draw descriptor recreation, and immediate patching of a material binding
still referenced by old packets. A shader package must validate the target layout;
a conceptual group does not prove an identical binding implementation everywhere.

## Predictable dependency planning and attachment bandwidth

Kapoulkine's synchronization discussion connects barrier precision to knowledge
of future resource use. It contrasts just-in-time tracking with declared graph
dependencies, notes the author's preference for explicit final pass order, and
warns that putting all streaming work in a frame graph can increase complexity.
The render-pass section explains load/store intent and attachment resolves as
ways to avoid unnecessary memory traffic, particularly on tile-based GPUs.

**Ludus refinement:** Begin with an authored ordered pass list compiled into a
dependency graph. Track resource versions and RAW/WAR/WAW hazards, reject illegal
feedback and undefined loads, and keep a cross-execution import ledger. Lower
semantic dependencies in each backend, with exact consumer stages. Upload
scheduling remains separate and publishes import dependencies. Describe
Clear/Load/Discard, Store/Discard and resolve intent explicitly; add pass merging
or native subpass optimization only when measured.

**Excluded:** Heuristic pass reordering as the initial default, public Vulkan
subpasses, blanket all-stage barriers, and treating GPU queue order alone as
sufficient memory visibility. The article's historical statement about missing
barrier validation is not today's tooling contract: use current synchronization
validation, while still testing cases that validators cannot establish.

## Pipeline variants and compilation latency

Kapoulkine separates just-in-time pipeline lookup, driver cache persistence,
prewarming from observed combinations, and ahead-of-time knowledge of technique
state. Including complete state in technique definitions improves predictability,
but permutations multiply and need deliberate control. Native driver compilation
still occurs; source compilation and pipeline readiness are separate facts.

**Ludus refinement:** Have content/settings enumerate required canonical
technique variants and request them before use. Draw packets retain ready
pipeline handles. Record missing variants and pending/failed status, provide a
ready compatible fallback, and separate portable manifests from driver cache
blobs. Prewarm with budgeted workers or verified asynchronous backend support;
no cache miss creates a pipeline inside draw encoding.

**Excluded:** Pointer-derived persistent keys, raw padded state hashing,
unbounded permutation generation, and a promise that shipping SPIR-V/WGSL
eliminates driver compilation. The article motivates the policy; the exact cache
protocol and safe publication design are Ludus choices.

## Staging and GPU completion

Moore's console streaming pool uses GPU copies and a staging ring whose entries
are reusable only after their copy fence completes. He separates resource data
from CPU headers, leaves eviction priority to application logic, and describes
copy budgets and fragmentation problems. Platform-specific sections make clear
that the implementation depends on the console memory/API model.

**Ludus refinement:** Distinguish logical resource identity, retained CPU records,
physical allocation, source consumption, queued upload and destination readiness.
Return upload/readback tickets and apply backpressure to bounded rings. Preserve
staging until the actual backend consumption/completion obligation ends. Keep
eviction/residency policy with content, and show pending retirement in memory
reports. Future external compute needs the same explicit ownership discipline.

**Excluded:** General live GPU-pool compaction, direct CPU patching of moved native
resource addresses, console alignment constants, and a copy-every-frame policy.
Moore's ordered console stream does not prove Vulkan cross-queue synchronization
or WebGPU mapping rules. New Vulkan objects/bindings and verified dependencies
are required for any future relocation; native allocations cannot be treated as
freely movable raw console memory.

## WebGL state and uniform meaning

Cozzi's two chapters offer complementary lessons. Delaying state updates until
use can remove selector errors and redundant API calls, while delaying a large
buffer/texture transfer until drawing can hurt CPU/GPU overlap. Explicit state
objects separate scene/object meaning from global GL state; derived values can
be recomputed only when their inputs change.

**Ludus refinement:** Give WebGL one private context state cache and indexed
revisions/dirty ranges. Apply complete pipeline/binding snapshots at draws and
schedule bulk uploads earlier. Reset the cache on recreation and invalidate it
at any deliberate foreign-call boundary. At renderer level, prepare explicit
View/Material/Draw data; generate numeric layout mappings offline and cache
derived matrices in the data owner.

**Excluded:** Virtual observer/factory objects per uniform, a global string-to-
factory registry, runtime shader-source parsing, and uniform name lookup during
draws. The modern uniform-buffer and reflected-binding path carries the useful
meaning/state separation with less object and lifetime machinery.

## Recording granularity and latency

Schertenleib treats a batch as the data and state required to render an object,
shows how scattered scene/library access can inhibit throughput, and discusses
parallel display-list generation and CPU/GPU synchronization. His buffering
examples expose a latency/memory tradeoff. Kapoulkine adds Vulkan-specific pool
ownership and cautions against very small command buffers and excess submits.

**Ludus refinement:** Extract compact immutable packets, set execution order
before recording jobs, assign private completion-scoped pools to native workers,
and submit through one owner. Keep small passes serial and batch compatible
work. Measure input-to-display latency, memory, worker join cost, and CPU/GPU
frame time when selecting concurrency and buffering.

**Excluded:** Automatic one-thread-per-subsystem partitioning, one submission
per draw, guaranteed speedup from more worker threads, and a hardcoded historical
submit/millisecond budget. Browser WebGL still uses its context owner; a native
parallel-recording path does not establish browser thread support.

## Current primary source cross-check

These sources were consulted for constraints and design comparison. They do not
prove Ludus implementation support or transfer performance results. Latest
specifications/documentation may describe features beyond the pinned SDK;
actual enablement requires a version check and backend conformance.

| Primary source | Constraint or comparison read | Resulting Ludus decision |
| --- | --- | --- |
| [WebGPU specification](https://www.w3.org/TR/webgpu/), synchronization, buffer mapping, queue and pipeline sections | Render-pass usage scopes include bound resources; mapping and queue completion are distinct; asynchronous pipeline creation has explicit readiness | Validate whole scopes, keep mapping state separate, and use generation-checked asynchronous status supported by the pinned C API |
| [WebGL 2 specification](https://registry.khronos.org/webgl/specs/latest/2.0/) | Core resource/draw/copy operations and short client-wait limits | Restrict portable operations and poll completion across event-loop turns |
| [WebGL feedback rules](https://registry.khronos.org/webgl/specs/latest/1.0/) | Conflicting texture/framebuffer read/write produces an error | Reject feedback in the declared plan before executing |
| [Vulkan synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html) | Access/stage/layout dependencies, including execution-only WAR cases | Maintain a small tested semantic mapper rather than expose native flags |
| [Vulkan present retirement](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html) | Submit fences do not establish presentation completion; maintenance1 provides presentation fences | Preserve separate render and presentation retirement |
| [VMA quick start](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html) | Private single-implementation integration, explicit errors, no C++ exception requirement | Use a pinned backend adapter while keeping allocation metadata out of public headers |
| [Epic RDG](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine), debugging/validation | Deferred execution can obscure setup locations; individual optimizations and parallelism can be disabled | Add setup-context diagnostics and serial/no-alias/no-cull investigation controls |
| [D3D12 allocator reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandallocator-reset) and [Metal synchronization](https://developer.apple.com/documentation/metal/resource-synchronization) | Reuse/synchronization obligations depend on GPU work and the selected native model | Keep native details private and require common lifetime/dependency conformance |
| [CUDA interoperability](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html) | Device identity matching, external memory and synchronization protocols | Plan a separate compute executor and explicit native bridge; no implicit cross-API sharing |

The WebGPU specification exceeded the web reader's size limit, so the relevant
sections were fetched directly from the official W3C document and read as text.
Apple's synchronization discussion was read through its official Markdown
representation because the HTML page requires JavaScript.
The NVIDIA skill catalog was checked for the CUDA boundary; this broad engine
architecture did not require installing a product-specific skill. No skills,
repository dependencies, or toolchain settings were changed.

## Other mapped candidates

The map also identifies Camera-Centric Engine Design for Multithreaded Rendering,
A Generic Multiview Rendering Engine Architecture, A Generic Handle-Based Resource
Manager, resource-management chapters, shader interfaces, and cross-platform
rendering-thread articles. Their full text was not read in this task, and no
specific mechanism is attributed to them. They are useful next candidates when
implementing multiview, identity integration, streaming or a dedicated render
thread. Existing Ludus identity/memory decisions were checked directly for
compatibility, rather than presenting earlier reviews as new chapter reading.

## Resulting design and remaining proof

The reviewed design makes allocation and ownership more concrete without adding
a universal GPU runtime. Its first implementation remains one device, one owner,
one physical queue, reflected immutable resources/bindings, completion-based
reuse, and a serial ordered pass compiler. CUDA/other compute domains and native
advanced rendering stay explicit extensions.

The books do not prove correctness of Ludus's handle retention, graph compiler,
callback publication, external-state ledger, or cross-API bridge. The proposed
implementation phases must establish those through adversarial tests, native and
browser rendering, installed-SDK checks, and measurements on target devices.
