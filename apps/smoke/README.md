# Ludus smoke application

The native and browser executables share Application start/tick/shutdown and a
small bounded simulation. Private drivers choose their rendering and scheduling.
Native diagnostics startup, logging and profiling capture remain in main.cpp.
Native Vulkan command recording is unchanged; rendering failures now stop the
smoke loop and return a failure code instead of logging forever.

The web presets build out/build/<preset>/apps/smoke/index.html, index.js and
index.wasm. Serve over localhost or HTTPS; file:// is not the supported path.
The browser driver polls async RHI and pipeline readiness from RAF callbacks.
It never draws while loading. A failed/lost session releases sample GPU resources,
shuts down RHI, then destroys the window/listeners. Restart resets the simulation
and starts a fresh generation. Stop is idempotent.

Click the canvas, then use WASD/arrows to move the triangle. Click or drag to
reposition it; input uses the Platform snapshot and CSS pointer coordinates.
Visible animation and movement use at most 100ms of delta per tick. Hidden
canvases freeze simulation and skip rendering; held input clears through Platform.
The triangle stays inside the canvas through a square viewport on any aspect/DPR.
The renderer privately owns WGSL and pipeline resources through W5 sample interop;
no new public RHI or installed SDK API is introduced.

The shell provides loading, missing-WebGPU, failure and device-loss messages,
Restart/Stop and user-initiated fullscreen. Rendering diagnostics stay in the
console. Fullscreen permission/availability errors appear separately so frame
status updates cannot erase them. Status text changes only when its message
changes, avoiding repeated live-region announcements.

The production index build is web-only and contains no controlled provider.
A separate smoke-test.js/node target links the explicit test DOM/GPU provider and
runs the same Application/renderer sources. CI publishes only index.* artifacts.
Native Catch2 tests verify shared timing, hidden/blurred input and pointer mapping.
For hardware acceptance and the next packaging stage, see
../../docs/development/webgpu-w6-evidence.md.
