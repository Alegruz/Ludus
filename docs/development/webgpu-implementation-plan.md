# WebGPU browser export implementation plan

Created: 2026-09-30. Repository baseline inspected: `6ebdf7c`.
Status: W2 browser logging and profiling implemented; full engine browser export
is not yet implemented. W0 flag-free GPU acceptance remains pending. See `webgpu-w0-evidence.md` for actual validation and pending gates.

## Outcome and scope

Build Ludus C++23 code as WebAssembly, render through browser WebGPU, and
produce a self-contained ZIP suitable for itch.io's HTML5 upload. Preserve
Linux/Wayland/Vulkan support and the installed native SDK.

The first deliverable is an interactive smoke demo, not a complete game engine.
Completion means the demo renders, accepts input, survives resize and focus
changes, reports unsupported devices clearly, and works in a real itch.io embed
on a documented browser/OS/GPU combination without experimental browser flags.
Audio, persistence, gamepads, networking, a general asset pipeline, and native
WebGPU are follow-up features. There is no WebGL fallback in this plan.

## How to use this plan across agent sessions

Run one stage at a time, in order. A stage may take multiple sessions. Read its
handoff before continuing; verify the current files rather than trusting an old
checkbox. Preserve unrelated user changes. Do not commit, publish, or upload
unless authorized for that action. This plan authorizes no future actions by
itself; the implementation prompts below define the requested session scope.

Use this prompt to start or resume implementation:

> Read AGENTS.md, .kiro/steering/, and
> docs/development/webgpu-implementation-plan.md. Implement the first incomplete
> stage whose prerequisites are satisfied. Follow its scope and acceptance
> criteria, preserving Linux/Vulkan behavior and unrelated changes. Finish the
> stage if possible; otherwise leave a precise handoff. Run the applicable
> validation, update the progress table and session log with actual evidence,
> and report remaining blockers. Do not advance to another stage. Use a separate
> PR for the stage; use draft status while acceptance gates remain unverified.
> Do not merge or publish the game. WebGPU is the chosen browser renderer; do
> not add a WebGL fallback.

For a specific stage, replace “the first incomplete stage” with its ID. To
review progress without implementing, use:

> Audit this plan's progress against the current repository and recorded test
> evidence. Correct stale status entries, identify the next actionable stage,
> and report any missing validation. Do not change engine code.

At the end of every session, append: stage ID; changed files; commands and
results; browser/OS/GPU when relevant; evidence locations; remaining failures;
API decisions; and the exact next action. Use `not started`, `in progress`,
`blocked`, or `complete`. Complete requires all acceptance criteria; lack of a
GPU, upload access, or a test tool is missing evidence, not a passing test.

## Architectural decisions

- Use Emscripten and the Emdawnwebgpu C API, with third-party WebGPU/Emscripten
  headers private to implementation files. Pin a tested toolchain/port pair in
  stage W0; do not use floating latest versions or obsolete `-sUSE_WEBGPU`
  recipes. Confirm compile and final-link flags for that pinned pair.
- Select Vulkan on Linux and WebGPU on Emscripten at build time. Initially keep
  a single compiled RHI backend per build, avoiding a runtime plugin framework.
- Use a single browser main thread. No pthreads, SharedArrayBuffer, blocking
  waits, or dependency on cross-origin isolation for the first release.
- Adapter/device requests are asynchronous. Add an explicit startup state
  contract rather than returning success before the device is ready. Suggested
  states: uninitialized, initializing, ready, failed, lost, stopped. Define
  allowed transitions and callback lifetimes before coding the backend.
- Use browser frame callbacks through Emscripten. Keep application state alive
  after `main()` returns; do not retain pointers to stack-local startup data.
- Keep normal logging and FoundationBase's independent failure path separate.
  Browser diagnostics must not require Unix sockets, a Python helper, worker
  threads, or the normal logger during an assertion failure.
- Maintain C++23, exception-free production code, explicit errors, Ludus numeric
  aliases, include boundaries, naming rules, and native validation gates.
  Platform capabilities must determine diagnostic behavior; never spoof Linux
  or globally disable assertions just to cross-compile.

## Current blockers and likely change locations

| Area | Evidence in current tree | Work needed |
| --- | --- | --- |
| Foundation | `modules/foundation/base/CMakeLists.txt` rejects non-Linux; Linux diagnostic sources | Browser platform/failure implementation and build selection |
| Configuration | Base `config.h` recognizes desktop OS and x86_64/ARM64; `compiler.h` defines debug traps | Emscripten/wasm detection and validated browser trap behavior |
| Build | Linux-only presets and native assumptions in `scripts/python/engine.py`; Conan always requires Volk | Separate pinned web dependency/toolchain path and conditional dependencies |
| Diagnostics | `tools/diagnostics/src/session.cpp` uses Unix sockets; smoke links integration | Native-only integration or browser-safe session implementation |
| Logging | `src/internal/backend.cpp` starts `std::thread`; sinks assume native output/files | Single-thread browser dispatch and console sink |
| Profiling | Clock/thread identity and file export need a portability audit | Browser-safe clock/identity; explicit virtual-file/download behavior |
| Platform | `window.cpp` selects Wayland/headless; `NativeWindowInfo` stores Wayland handles | Canvas backend and a backend-neutral borrowed window descriptor |
| RHI | `rhi.cpp` is Vulkan; public lifecycle assumes synchronous initialization | Backend separation, async readiness, WebGPU surface/frame lifecycle |
| App | `apps/smoke/main.cpp` runs a blocking loop and native shutdown/export | Shared tick with separate native/browser lifecycle drivers |
| Delivery | No HTML shell, WGSL assets, or browser package target | Shell, packaging, browser QA, and deployment instructions |

Paths are starting points, not permission for broad refactoring. Check nested
instructions and relevant ADRs before modifying each area.

## Stages and acceptance criteria

### W0 — Feasibility, toolchain, and deployment probe

Prerequisites: none. Deliver a small isolated C++/WebAssembly WebGPU probe,
reproducible build instructions, and an ADR describing the selected integration.
Use a tools/example target outside engine modules; do not bypass the engine's
Linux guard by pretending Emscripten is Linux.

1. Select and pin a released Emscripten SDK and matching Emdawnwebgpu package or
   port revision. Record versions, LLVM/libc++ versions, download provenance,
   licenses, and CMake integration. Verify the needed C++23 facilities,
   formatting/conversion support, and exception-free linking with a probe.
2. Resolve toolchain policy explicitly: the native reference is Clang 18;
   Emscripten supplies its own LLVM. Document the added web toolchain and update
   applicable repository toolchain rules before engine implementation. Keep
   clang-format/clang-tidy 18 for repository checks; test their ability to analyze
   the pinned web headers and flags rather than silently changing versions.
3. Demonstrate asynchronous adapter/device creation and an animated canvas
   clear, using the actual C API that the engine will use. Add a visible failure
   message for missing WebGPU, null adapter, and device request failure.
4. Build a ZIP with root `index.html` and all runtime files. Test localhost,
   an HTTPS iframe, and an itch.io draft/private embed when upload is authorized.
   Record permissions/sandbox restrictions and actual GPU availability.

Acceptance: pinned probe builds and runs on a real WebGPU device without browser
flags; exact versions and asynchronous callback behavior are recorded. Actual
itch.io embedding must be proven before W8 is complete. If upload access is
unavailable, record that gate as pending and continue independent local work.

### W1 — Cross-compilation and FoundationBase

Prerequisites: W0's toolchain decision and local probe pass. On 2026-09-30 the
user authorized continuing after a real itch.io render with Vulkan enabled;
independent W1 CPU-only work proceeds, while W0's flag-free GPU gate remains
open and must be resolved before declaring deployment support.

Add proposed `web-emscripten-development` and `web-emscripten-release` presets,
isolated `out/` trees, and bootstrap/doctor/build support. Teach CMake and Conan
or the selected separate web dependency path to omit Volk and Wayland on web.
Never reuse native Conan artifacts for wasm. Select target-appropriate sources
and tests; native Python death tests must not execute wasm binaries directly.

Add browser/wasm configuration macros, private diagnostic platform/output
implementations, and platform-aware generated assertion capabilities. Audit
signal handling, Linux syscalls, debugger detection, trap/termination, control
transport, and build-flavor policy. Preserve Require/Fatal termination and
Check return/report behavior; specify enabled Assert behavior without desktop
dialog support. Exclude native helper startup from the web target graph.

Acceptance: a FoundationBase-linked browser executable builds in both flavors;
browser failure semantics have dedicated probes; native assertion/policy tests
still pass. Foundational header gates and exception policy remain enforced.

### W2 — Browser logging and profiling

Prerequisites: W1.

Implement synchronous browser logging behind existing interfaces, retaining
levels, categories, breadcrumbs, bounded error handling, and safe repeated
startup/shutdown. Do not construct a worker thread in web builds. Route output
to a browser console sink; define unsupported file/debugger sink behavior.
Audit locks and waits even when they compile under Emscripten.

Port profiling clock and thread identity; retain enabled/disabled build-flavor
behavior. Specify virtual filesystem semantics and offer an explicit browser
download for traces if export is supported. Do not claim virtual files are
persistent host files. Validate sink failure and logger-independent assertions.

Acceptance: browser logging and profiling probes execute without pthreads;
Release instrumentation compiles out as specified; native logging/profiling
tests pass. No browser startup waits on background workers or native endpoints.

### W3 — Canvas platform and input

Prerequisites: W2.

Implement a browser window backend under `modules/platform/`, with Emscripten
and DOM integration behind private boundaries. Extend the native-window
descriptor to represent a borrowed canvas target with an explicit lifetime,
without leaking WebGPU handles or private headers. Preserve Wayland consumers.

Handle CSS dimensions versus framebuffer pixels, device pixel ratio, GPU size
limits, zero-size/hidden canvases, focus/blur, visibility, keyboard, pointer,
wheel, and resize. Define input state/event ownership and clear held input on
blur. Browser callbacks must be unregistered or invalidated during shutdown.
Only suppress browser shortcuts/scrolling when the focused game needs it.

Acceptance: a browser platform probe displays resize/input state; focus loss
cannot leave keys stuck; repeated startup/shutdown does not duplicate callbacks;
native Wayland/headless tests and header gates pass.

### W4 — RHI split and asynchronous lifecycle contract

Prerequisites: W3.

Move Vulkan implementation behind backend selection without changing its frame
behavior. Define backend-neutral startup/readiness/failure status and revise
public Vulkan-specific comments. Preserve existing synchronous entry points
as native conveniences where useful, while the web app uses the async contract.
Record public API/SDK compatibility changes and migrate consumers explicitly.

Implement WebGPU adapter/device/surface acquisition with minimal requested
features, negotiated limits, error callbacks, and owned handles. Define cancel,
shutdown during pending requests, stale callback invalidation, repeated startup,
and device loss. Callbacks must never touch destroyed application state.
Rendering before ready must return an explicit status without blocking.

Acceptance: meaningful lifecycle tests cover success, request failures, shutdown
while pending, duplicate operations, and stale callbacks. Native RHI tests and
SDK consumer pass; the browser reports ready only after device/surface setup.

### W5 — WebGPU frames and rendering proof

Prerequisites: W4.

Implement surface capabilities/configuration, preferred format, current texture
acquisition, command encoding, clear pass, queue submission, and per-frame
resource release using the pinned API. Do not emulate Vulkan presentation calls
that have no equivalent in browser WebGPU. Treat unavailable frame/resize
conditions as recoverable status when appropriate.

Add a WGSL triangle or small quad pipeline to prove shader compilation and draw
submission in addition to clearing. Keep demo-specific shader/resources private
to a sample; add only the minimal engine API needed for the agreed smoke scope.
Handle asynchronous shader/pipeline errors where supported. Reconfigure on
resize, skip zero-size frames, and show a controlled device-loss/reload message.
Automatic device recovery is optional future work; uncontrolled continuation is
not acceptable.

Acceptance: visible animation and geometry on real hardware; resize/fullscreen
does not stretch incorrectly or produce validation errors; GPU handles are
released; injected lifecycle failures stop rendering safely. Record screenshot
and browser console evidence. Native Vulkan rendering still works.

### W6 — Integrated browser smoke application

Prerequisites: W5.

Refactor the smoke application's initialization/tick/shutdown into reusable
operations with separate native and browser drivers. The browser driver yields
to frame callbacks and waits for RHI readiness through state transitions. Bound
delta time after tab suspension. Provide input-driven visible behavior and
explicit shutdown/reload handling; keep state alive across callbacks.

Add an HTML shell with canvas, loading/error state, WebGPU capability check,
and fullscreen behavior initiated by user action. No developer implementation
details should appear in the normal demo flow. Browser errors may expose a
concise useful message and diagnostic details in the console.

Acceptance: the integrated app builds in both web flavors and native presets;
loads, animates, responds to input, resumes after backgrounding, and shuts down
safely. Unsupported WebGPU produces a readable message rather than a blank
canvas. No GPU resources are accessed while initialization is pending.

### W7 — Reproducible itch.io package

Prerequisites: W6.

Add a proposed `scripts/package-web` command integrated with existing tooling.
It stages Release files under `out/` and produces a ZIP with `index.html` at the
root, generated JS/wasm and any data/WGSL files at their expected relative paths,
and required license notices. Keep dependencies local to the package; avoid CDN
runtime fetches and checkout-absolute paths. Describe packaged assets, startup
ordering, memory limits/growth, and debug-symbol handling explicitly.

Check current itch.io limits and upload settings. Validate the ZIP contents,
extract into a clean directory, serve over localhost, and check every requested
file. Provide an HTTPS deployment guide; opening `file://` is not the supported
test path. Include an iframe smoke harness without special isolation headers.

Acceptance: one documented command produces a self-contained artifact from a
prepared clean checkout; extracted package runs; missing asset/network failures
produce readable errors; no developer paths or unnecessary debug files ship.

### W8 — Browser CI, hosted acceptance, and handoff

Prerequisites: W7; authorization/access for an actual itch.io test upload.

Add pinned web compile and meaningful browser tests to CI. Separate software-GPU
or mocked lifecycle tests from real-device evidence; neither proves hardware
compatibility by itself. Keep existing native gates. Browser tests should cover
startup readiness, frame output, input, resize, blur, tab suspension, missing
WebGPU, denied adapter/device, device loss, and callback lifetime failures.

Validate the exact Release ZIP in an itch.io draft/private embed on a supported
real GPU without experimental flags. Record browser versions, OS, GPU/backend,
embed URL, artifact hash, console output, and tested input/fullscreen behavior.
Document a tested support matrix; treat other combinations as unverified rather
than asserting universal browser support. Publish only if separately requested.

Acceptance: all applicable native/web gates pass; actual itch.io embed evidence
exists; user-facing build/upload instructions reproduce the artifact. Mark the
overall feature complete only after hosted acceptance. A smoke demo's completion
does not imply audio, saving, or other game systems have been ported.

## Validation policy

For implementation stages, use the pinned native tools and required repository
checks: native build/unit tests, `./scripts/check linux-clang-development --all`,
ASan/UBSan build/tests, and installed SDK consumer when public/build boundaries
change. Keep header self-sufficiency, foundational includes, and build budget
gates intact. Record precise commands, including warnings-as-errors settings.
Native sanitizers cannot validate the JavaScript/WebGPU boundary; add browser
execution and relevant pinned Emscripten diagnostics. Web static analysis must
include web-only translation units; native tidy alone is insufficient.

If a gate cannot run, record the reason and next command; do not mark the stage
review-ready. Avoid changing rules or suppressing warnings merely to obtain a
green run. Editing only this plan does not require engine builds.

## Progress tracker

| Stage | Status | Evidence / remaining work |
| --- | --- | --- |
| W0 | in progress | Actual itch.io core-adapter render proven with Vulkan enabled; continuous animation and flag-free support remain pending. See webgpu-w0-evidence.md |
| W1 | complete | CPU-only FoundationBase browser port, Development/Release probes, native regressions and SDK consumer validated. See webgpu-w1-evidence.md; W0 GPU gate remains open |
| W2 | complete | Synchronous browser logging, bounded failure/reentry probes, browser clock, virtual trace export/download, Release compile-out and native regressions validated. See webgpu-w2-evidence.md |
| W3 | complete | Canvas/DPR/input lifecycle and bounded ownership, both web probes, native regressions and SDK validated. See webgpu-w3-evidence.md; CI pending |
| W4 | implemented; acceptance pending | Backend split and async lifecycle tested in wasm/native; real-GPU Ready check remains pending. See webgpu-w4-evidence.md |
| W5 | not started | Depends on W4 |
| W6 | not started | Depends on W5 |
| W7 | not started | Depends on W6 |
| W8 | not started | Depends on W7 and hosted test access |

## Session log

- 2026-09-30: Created plan after inspecting build, FoundationBase, logging,
  profiling, platform, RHI, smoke app, and repository policies. No implementation
  or browser/hardware validation performed. Next action: W0 toolchain/probe.
- 2026-09-30 W0: User authorized implementation using PRs. Isolated branch
  `codex/webgpu-w0` starts at updated main `29436c9`. Added the standalone C API
  probe, SDK/port lock, build/check/package command, compile workflow, ADR 0009,
  and explicit web toolchain policy. See `webgpu-w0-evidence.md` for commands,
  results, and limitations. Next action: execute the packaged probe on a real
  WebGPU device without flags, then complete authorized HTTPS/itch.io checks.
  Historical gate: W1 was initially gated on real-device local acceptance; see
  the 2026-09-30 continuation below.

- 2026-09-30 W1: User authorized continuing after the hosted probe rendered on
  Intel UHD 620/Edge/Linux with Vulkan enabled. Added isolated root browser
  presets, separate SDK bootstrap/doctor/build/test/check path, Base web platform
  and output implementations, common control codec, linked browser probes, and
  CI coverage. See `webgpu-w1-evidence.md` for validation and limitations. Native
  dialog/debugger policy is unchanged. Browser enabled ASSERT is terminal; CHECK
  returns after reporting; native transport requests fail without blocking.
  Next implementation stage at that point: W2 logging/profiling. Keep W0 flag-free GPU support
  and final W8 hosted-game acceptance open.

## Primary references

Consult these again when implementing; documentation and browser capabilities
change. The architectural choices above are Ludus design decisions, not claims
that every reference prescribes them.

- [Emscripten WebGPU integration](https://emscripten.org/docs/porting/multimedia_and_graphics/WebGPU-support.html): browser WebGPU through Emdawnwebgpu's C API.
- [Dawn Emdawnwebgpu integration/build documentation](https://dawn.googlesource.com/dawn/+/refs/heads/main/src/emdawnwebgpu/README.md): package and CMake integration details; confirm against the selected release.
- [Emscripten compiler settings](https://emscripten.org/docs/tools_reference/settings_reference.html): current port/exception/memory settings; avoid obsolete WebGPU flags.
- [Emscripten runtime environment](https://emscripten.org/docs/porting/emscripten-runtime-environment.html): browser event loop, virtual files, and application lifetime.
- [WebGPU adapter acquisition](https://developer.mozilla.org/en-US/docs/Web/API/GPU/requestAdapter): asynchronous acquisition, secure contexts, and adapter availability.
- [itch.io HTML5 upload requirements](https://itch.io/docs/creators/html5): ZIP entry point, relative assets, limits, and embed settings.

- 2026-10-01 W2: Implemented browser logging/profiling on merged W1. Both
  browser configurations pass eight wasm/build-contract tests and browser page
  execution. Native Development, ASan/UBSan, format/tidy, and SDK consumer checks
  pass. See `webgpu-w2-evidence.md`. Next stage: W3 canvas platform/input.
  W0's flag-free GPU support gate remains open.

- 2026-10-01 W3: Added browser Platform backend, borrowed canvas descriptor,
  copied input snapshots/events, CSS/DPR/clamped framebuffer dimensions and
  callback teardown. Development/Release wasm and real DOM probes pass. Native
  Development, ASan/UBSan, format/tidy and SDK consumer pass with the existing
  no-display Wayland skip. See `webgpu-w3-evidence.md`. Next stage: W4 RHI split
  and asynchronous lifecycle contract. W0's hardware gates remain open.

- 2026-10-01 W4: Split the native Vulkan implementation from the lifecycle facade;
  added browser adapter/device/surface startup, explicit states, cancellation,
  stale-token invalidation and owned teardown. Nine controlled-provider wasm
  scenarios and native regressions pass. Native SDK consumers migrated. The real
  in-app browser reports adapter unavailable; target-browser Ready verification
  remains pending. See `webgpu-w4-evidence.md`. Next implementation: W5 frames,
  after checking W4 merge/CI and completing the target-browser readiness check.
