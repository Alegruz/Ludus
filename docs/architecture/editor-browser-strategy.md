# Browser access to Ludus

The current increment ports the existing Qt Widgets editor to WebAssembly and
includes it in GitHub Pages. This is a bounded workspace preview: project and
configuration documents can be edited, imported and downloaded. Native tools,
game execution/debugging and audio authoring remain desktop workflows. See the
[browser editor guide](../wiki/guides/browser-editor.md) and
[reproducible build instructions](../development/browser-editor.md).

Keep Qt as the shared editor shell. The browser uses file and process adapters;
future macOS support should use the same shell with native adapters. The engine
renderer belongs in the game viewport, with a working backend for each platform.
Separate UI implementations are not justified by this first port.

## Existing footing and missing pieces

Ludus already has a pinned Emscripten/WebGPU toolchain, browser Foundation,
canvas Platform and RHI lifecycle/frame-clear targets, an isolated triangle probe,
and the integrated W6 smoke app. These are runtime foundations, not a browser
scene editor. See [ADR 0009](../decisions/0009-browser-webgpu-toolchain.md),
`config/web_toolchain.json`, and `apps/smoke`.

The native editor currently targets Linux x64. Its controller owns
QProcess-based Python/build/debugger/Play adapters. A browser cannot simply start
the user's local CMake/compiler/RAD process. The WebAssembly target uses single-threaded static Qt, Asyncify modal dialogs
and explicit file import/download.
[Qt's WebAssembly documentation](https://doc.qt.io/qt-6/wasm.html) describes
asynchronous constraints and matching Qt/Emscripten builds. Do not infer that our
pinned engine WebGPU SDK is ABI-compatible with Qt's current Wasm requirements.

Browser storage also differs from desktop project folders. OPFS is origin-private,
quota-limited storage and is removed when site data is cleared; provide explicit
project export/import and recovery. [MDN OPFS documentation](https://developer.mozilla.org/en-US/docs/Web/API/File_System_API/Origin_private_file_system).
Emscripten's default MEMFS is volatile; IDBFS persistence requires explicit
synchronization. [Emscripten filesystem documentation](https://emscripten.org/docs/api_reference/Filesystem-API.html).

## Investment order

The ROI ranking below is engineering judgment based on current code and missing
services. It is not a measured market forecast or a calendar/cost estimate.

| Rank | Product slice | Relative investment | Value and gate |
| --- | --- | --- | --- |
| 1 | Website with a playable Ludus demo, documentation and downloads | Low/medium, reuses existing runtime | Lets people try the engine without setup. Ship after browser/device startup and failure UX are reliable |
| 2 | Browser content playground: inspect/tune a small authored example and export changes | Medium, bounded data and operations | Useful for learning/sharing. Requires S3/S4 document contracts and an actual editable vertical slice |
| 3 | Browser editor for curated asset/scene workflows | High, storage/import/render/input integration | Potential reach and onboarding value. Proceed only after round-trip correctness and representative performance are proven |
| 4 | Full online C++ engine development with arbitrary projects/builds | Very high, ongoing service operation | Adds isolated builds, queues, quotas, authentication, artifacts, cancellation, storage and debugging. Validate demand and an operating budget before committing |

A static website can host Wasm runtime artifacts. Full arbitrary C++ builds need
an execution strategy: curated precompiled modules, a substantial in-browser
compiler/toolchain investment, or isolated remote build workers. Remote builds
trade local setup for service cost and infrastructure ownership. A downloaded
browser application is not automatically a complete development environment.

## Shared contracts and later expansion

The preview reuses existing private Qt models, validation and serializers. It
does not turn them into a portable editor SDK. Native process, source-watch and
audio adapters are replaced explicitly; the browser does not simulate successful
builds. Files use volatile session memory and explicit downloads. Persistent
project storage and a scene/game viewport are later increments requiring their
own round-trip and performance evidence.

For cloud compilation, keep the operation protocol and cleanup/revision contract,
but execute jobs in isolated workers with resource limits and explicit ownership.
The web UI displays the same operation states. User project code does not execute
inside the API service or UI thread. Account, collaboration and billing work follows
validated demand, not the first browser demo.

## Go/no-go evidence

Use the W6 smoke baseline and one representative asset/scene task. Before further
investment, define target browsers/devices and payload budgets; measure download
bytes, cold-start p50/p95, frame/input latency, memory, and failure recovery. Test
missing WebGPU, device loss, reload, quota failure, cancellation, persistence and
export/import correctness. Include keyboard focus and narrow-window tasks.

Advance to curated editing only when the sample round-trips with the desktop
format and users complete the intended task reliably. Advance to remote builds
only with observed demand for C++ online authoring and an explicit cost envelope.
The Qt workspace preview is narrower than a scene editor or online C++ IDE.
Its package and browser document checks do not establish those later capabilities.
