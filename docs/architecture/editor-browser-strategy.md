# Browser access to Ludus

Decision for this increment: a browser experience is feasible, but full desktop
editor parity is a separate product investment. Prioritize a public runtime/demo
and later a bounded content-editing experiment; keep S2 desktop interactions on
track. This is a feasibility/ROI assessment, not a hosted service or deployment.

## Existing footing and missing pieces

Ludus already has a pinned Emscripten/WebGPU toolchain, browser Foundation,
canvas Platform and RHI lifecycle/frame-clear targets, an isolated triangle probe,
and the integrated W6 smoke app. These are runtime foundations, not a browser
scene editor. See [ADR 0009](../decisions/0009-browser-webgpu-toolchain.md),
`config/web_toolchain.json`, and `apps/smoke`.

The current editor is explicitly Linux x64 native Qt Widgets. Its controller owns
QProcess-based Python/build/debugger/Play adapters. A browser cannot simply start
the user's local CMake/compiler/RAD process. Qt has a WebAssembly port, but its
browser event-loop and filesystem boundaries require deliberate adaptation.
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

## Proposed architecture for a later experiment

Reuse document schemas, stable IDs, validation, command semantics, revision checks
and asset formats. Keep browser storage/render/job transports as adapters around
those contracts. S2 should extract reusable document policy before introducing a
second UI; existing private Qt models are not yet a portable editor SDK.

For the first experiment, use one supported sample with local browser editing,
Worker-based heavy processing, explicit Save/Export, and desktop round-trip import.
Do not promise native debugger/hot-reload parity or embed the desktop UI wholesale.
Evaluate a web-native shell versus a small Qt Wasm proof using the same task and
payload; choose on download size, input quality, maintenance and debugging evidence.

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
The art-direction study in this PR is an interactive UX prototype, not evidence
that the runtime or editor is deployed online.
