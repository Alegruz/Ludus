# WebGL 2 fallback implementation handoff

Prepared for Kiro on October 2, 2026. This is a proposed design and implementation contract, not evidence that WebGL support exists. The goal is to make the Ludus browser demo and the separate Drift ocean playground playable when WebGPU cannot initialize, while preserving the existing Vulkan and WebGPU paths.

The user has authorized planning for WebGL support. For this work, this handoff supersedes the WebGL prohibition in `drift-ocean-prompts.txt` and the earlier WebGPU-only scope in ADR 0009. Retain Slang as the authored shader language; generated GLSL ES is an additional backend artifact. Record the implemented decision in a new ADR rather than rewriting historical acceptance evidence.

## Verified starting points

- Browser RHI currently selects `src/rhi_webgpu.cpp` at build time; native RHI selects Vulkan. Public `Backend` has only Vulkan and WebGPU.
- `rhi_webgpu.cpp` requests a WebGPU canvas context before asynchronous adapter creation. It already retries with the WebGPU compatibility feature level. That retry still requires WebGPU and is not a WebGL fallback.
- `render.h` exposes opaque shader, uniform and pipeline handles, bounded resource capacity, explicit resource status, frame information and fullscreen drawing. Extend this contract; do not invent a parallel public renderer for the ocean.
- `ShaderDescription` carries SPIR-V and WGSL artifacts and entries. `ludus_compile_shader` and `cmake/shaders/compile_shader.py` generate artifacts, layout evidence, manifests and dependency information for the installed SDK.
- `config/shader_toolchain.json` pins Slang 2026.1.2. Its ability to produce usable GLSL ES 3.00 for this slice has not been tested in this handoff.
- `apps/smoke` still uses private WebGPU triangle interop. `application.cpp` gathers browser input only when the selected backend is WebGPU; a new backend must fix that assumption.
- Existing hosted and public-rendering evidence distinguishes controlled providers, software GPU execution and physical GPU acceptance. Preserve that distinction.
- The screenshot shows a failed graphics startup and a controls panel overlapping the message. It does not identify the browser, origin, adapter failure cause or deployed revision. Drift's current Sandbox sources have not been inspected here.

Read the current checkout before implementing. Relevant entry points are `modules/graphics/rhi/src/rhi.cpp`, `src/internal/backend.h`, `src/internal/lifecycle.h`, `src/internal/resources.h`, both existing backends, the public RHI headers, `modules/platform`, `tests/sdk_consumer`, `tools/web-browser-tests`, and `scripts/package-web`. Read AGENTS.md, `.kiro/steering/`, ADRs 0003, 0004, 0007, 0009 and 0011, and both shader/fullscreen-rendering handoff documents.

## Scope and ownership

Implement WebGL **2**, using Emscripten's OpenGL ES 3 bindings, for the current fullscreen slice: vertex and fragment shaders, one uniform block, a non-indexed triangle, the main canvas, resize, error handling and restart. Preserve resource limits and ownership rules. Defer WebGL 1, textures, compute, general mesh rendering, render graphs and automatic migration during active gameplay.

Ludus owns backend selection, rendering resources, shader build tooling and platform lifecycle. Ludus-Sandbox owns the ocean shader, scene state, controls and game packaging. Consumers use the installed SDK; no GL, Vulkan, WebGPU handles or private headers enter their integration code. Runtime downloads or compilation of Slang are out of scope.

## Shader feasibility gate

Before adding production backend code, compile the existing diagnostic Slang shader to GLSL ES 3.00 using the pinned compiler. Desktop GLSL output is insufficient. Prove stage compilation, program linking and changing uniforms in a real WebGL 2 context. Record commands, output versions, generated entry points, required extensions and browser logs.

If direct output is unsuitable, evaluate a pinned build-time SPIR-V to GLSL ES translator, such as SPIRV-Cross, against this exact shader. Verify acquisition hashes, licensing, host compatibility, compiler flags and dependency tracking. The translated input must avoid unsupported Vulkan-only features. Do not fix generated shaders with an unvalidated version-string substitution or maintain a second handwritten ocean shader. If neither route preserves the authored source and rendering contract, document the concrete failure and the smallest decision needed before dependent work.

Verify std140 uniform layout independently, including offsets, block size, padding and binding association after link. Query active block layout in the real program where applicable; compare it with upload structs and generated metadata. A shared CPU layout is permitted only when measured compatible across all three outputs. Otherwise expose a clear backend-specific upload contract using engine types, without backend handles. Do not overload `UniformSize` with inconsistent meanings.

Check vertex index generation, fragment coordinates, resolution, Y orientation, aspect ratio and color output. Ludus math uses depth in [0, 1]; WebGL clip depth differs. Define and test any conversion and avoid double conversion by the compiler and backend. Use asymmetric markers and mixed scalar/vector uniforms so orientation and packing errors are visible.

## Backend selection and canvas ownership

Proposed policy is Auto by default, with explicit WebGPU and WebGL2 selection for diagnosis and tests. Native remains Vulkan. Keep existing startup call sites valid using an additive option or overload; decide the smallest API after inspecting the facade. Forced selection must fail explicitly rather than silently switching. Report the actually selected backend through `StartupInfo` once established.

Auto attempts WebGPU, including its existing compatibility retry. If the API, adapter, device or required surface capabilities are unavailable, clean up the attempted session and attempt WebGL 2 once. Keep startup pending during asynchronous work. Do not fallback on invalid window descriptions, malformed shaders, invalid handles, capacity exhaustion or arbitrary validation bugs. Shader failure after backend readiness is an application error, not a reason to conceal it with another backend.

Choose the backend before binding the live canvas context whenever possible: probe adapters/devices first and make surface acquisition the commitment point. Probe WebGL availability on a disposable canvas. A canvas that has acquired a WebGPU context cannot then acquire WebGL. If late surface failure requires replacement, Platform must own that replacement and rebind its listeners, resize observer, focus behavior and native window information. Never replace DOM nodes behind Platform's back. Specify and test one strategy before changing startup.

Use generation tokens for backend attempts as well as outer startup sessions. Late WebGPU callbacks must release returned objects and must not overwrite WebGL readiness, failure or a later restart. Shutdown cancels attempts before releasing resources. Retain bounded diagnostics for each failed attempt; final failure reports both paths without asserting that the GPU itself is unsupported.

Restart reselects according to policy and recreates resources safely. WebGL context loss transitions to the existing lost state, stops drawing and invalidates all resource handles. Permit browser restoration where required and make explicit restart recreate resources on a usable context. A restored context must never resurrect old handles. Specify callback registration, unregistration and canvas reuse/replacement rules. Avoid an unbounded retry loop or automatic live backend switching.

## Rendering and build integration

Add a private WebGL implementation and a small browser dispatch layer behind the existing facade. Avoid duplicate exported backend function definitions when both implementations are linked. Reuse resource validation and handle generation wherever practical; do not broadly refactor native Vulkan.

Use an ES 3 context, vertex array objects as required, generated GLSL ES stages, explicit uniform block binding and bounded uniform buffers. Reject missing or incompatible blocks before drawing. Preserve update timing, resource dependency checks, partial cleanup, frame abort and stale-handle semantics. Reserve resources at setup; no per-frame shader/program/buffer creation or engine heap allocations. Shader compile/link diagnostics use the engine logger.

Extend `ShaderDescription` and generated factories with the minimum GLSL ES data needed. A WebGL stage generally links through generated `main`; validate actual output rather than assuming entry names carry over. Define default framebuffer encoding and `FrameInfo` consistently; verify colors rather than claiming sRGB behavior from a name. Handle zero extent and hidden frames, DPR, negotiated texture/viewport limits, and viewport updates on resize.

Propagate the required WebGL 2 Emscripten link settings through the installed target. Verify the settings supported by the pinned SDK; keep WebGPU port propagation for Auto builds. Extend artifact manifests, tool pinning, generated headers, depfiles, byproducts and installed CMake/Python helper files. Include both browser artifacts in an Auto build. Native packages must keep working without a new runtime dependency. Update licenses if an extra host translator is shipped or acquired.

Update the smoke renderer to work on both browser backends, preferably through the existing public rendering API and generated generic shaders. Update input handling to recognize browser windows rather than one graphics backend. Do not export private probe APIs to solve this.

## User experience and Sandbox integration

Loading and fallback remain readable, with game controls hidden or disabled until playing. Place final errors in a reserved responsive area that cannot be covered by the ocean controls. Use concise player text, a functional restart action and technical details in logs/test metadata. An expected Auto fallback should not look like a fatal game failure.

After engine acceptance, inspect Ludus-Sandbox's rules and current integration. Rebuild its SDK dependency, regenerate the existing ocean shader for all required targets and upload the verified layout for the selected backend. Keep the same scene, controls and shader source. Produce an identified local browser ZIP with index.html at its root. Test extracted assets, localhost, HTTPS and the actual iframe separately. Creating a package does not authorize publishing or account changes; do not infer current upload authorization from historical engine evidence documents.

## Acceptance and evidence

| Scenario | Required observation |
| --- | --- |
| Auto with usable WebGPU | WebGPU selected; correct pixels and input; WebGL not initialized |
| Missing WebGPU API or adapter rejected | WebGL2 selected; frames advance and uniforms animate |
| Device rejection or late surface capability failure | One clean fallback; live canvas and callbacks remain consistent |
| Forced WebGPU or WebGL2 | Selected backend only; explicit failure if unavailable |
| Both unavailable | Final readable error; no busy loop, invalid draw or controls overlap |
| Invalid shader or program | Explicit resource error; partial objects released; no concealed fallback |
| Restart during pending adapter/device request | Late callbacks harmless; exactly one current session |
| Context loss and restoration | Old handles invalid; drawing stops; restart rebuilds successfully |
| Repeated stop/restart and resize | No accumulated listeners/resources; correct dimensions and input |
| Installed native and browser consumers | Public API only; generated artifacts/dependencies and exports work |
| Drift package in its real embed | Ocean animates, controls respond, status stays readable |

Use meaningful lifecycle/resource tests and controlled failures, plus actual browser compile/link/render tests. Inject context loss with `WEBGL_lose_context` when available, recording the extension dependency. Never destabilize a real driver to test loss. Include portrait/landscape, DPR, blur, visibility, fullscreen and narrow error layouts. Use numerical diagnostic pixels with stated tolerances, and screenshots of the real ocean as separate evidence.

Run warning-clean native and browser builds, unit tests, ASan/UBSan for affected native/shared paths, pinned format and tidy including changed wasm translation units, header self-sufficiency/foundational include gates, applicable build budgets and clean SDK install/consumer checks. Extend exact-package browser checks. Record unavailable checks explicitly; mock tests, software rendering and experimental browser flags are not physical flag-free acceptance.

Each implementation stage must deliver exact commands and outcomes, revision/diff identity, selected backend, browser/OS, flags, GPU information when accessible, screenshots/logs and remaining gates. Finish only after the relevant matrix rows pass or are explicitly marked unverified. No unconditional browser support claims or invented completion evidence.

## Reference documentation

- [Emscripten OpenGL and WebGL support](https://emscripten.org/docs/porting/multimedia_and_graphics/OpenGL-support.html) describes WebGL 2 targeting; verify flags against the pinned SDK.
- [Emscripten HTML5 API](https://emscripten.org/docs/api_reference/html5.h.html) documents context loss and restoration callbacks.
- [Canvas context rules](https://developer.mozilla.org/en-US/docs/Web/API/HTMLCanvasElement/getContext) explain why a canvas cannot switch context modes.
- [WebGPU adapter requests](https://developer.mozilla.org/en-US/docs/Web/API/GPU/requestAdapter) document secure contexts and null adapter results.
- [Slang compilation targets](https://shader-slang.org/slang/user-guide/targets) document GLSL output; they do not prove the pinned compiler produces WebGL-compatible GLSL ES for this application.
- [GLSL ES 3.00 specification](https://registry.khronos.org/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf) defines the target shader language.
