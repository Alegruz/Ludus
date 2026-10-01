# W4 RHI lifecycle evidence

Implemented 2026-10-01 from merged W3 (`4042770`). This stage establishes the
browser device/surface lifecycle; WebGPU frame rendering is reserved for W5.
Real-GPU Ready acceptance remains pending on the user's target browser.

## Contract and backend split

The common RHI facade selects a private Vulkan or WebGPU backend at build time.
The existing Vulkan frame implementation moved to rhi_vulkan.cpp; frame commands
and synchronization are unchanged. Managed startup rejects an invalid native
window before loading a GPU, acquires instance/window/device/rendering resources,
and returns Ready synchronously. Managed shutdown releases rendering commands
before device teardown and resets frame context storage for another session.

New Start/GetStartup APIs expose Pending, Ready, Failed and DeviceLost. Duplicate
Start is Busy until Shutdown, including after failure/loss. Rendering attempts
before Ready return NotReady without blocking. Unpaired frame calls return
InvalidState. Browser BeginFrameStatus is Unsupported until W5 implements frames.
The old bool/synchronous signatures remain as native conveniences and must not
be mixed with Start. The smoke app now uses Start and ends frames only when begin
succeeds. The installed consumer checks the new API and Idle after Shutdown.
Public comments and the RHI README describe this contract. Downstream binaries
must be rebuilt to use the new exported functions; no old signature is removed.

Web startup validates the existing canvas, creates the instance and surface,
requests a core adapter (compatibility retry on failure), then requests a device
with no optional features or elevated limits. It owns adapter/device/queue/surface/
instance handles and installs device-loss and uncaptured-error callbacks. Actual
negotiated device texture limits are published after surface configuration and
an asynchronous validation error scope succeeds. Configuration is a minimal 1×1
surface for W4; W5 owns framebuffer sizing, resize, texture acquisition and draws.
No WebGPU surface-present call or browser wait is introduced.

All lifecycle operations are main-thread-only and serialize one global session.
Callbacks carry opaque numeric generations rather than borrowed application
pointers. Generations never wrap. Shutdown invalidates the generation before
resource destruction; late adapter/device callbacks release their returned
handles without touching the active session. Pending browser promises are not
cancelled: the engine cancels its interest and cleans up late results. Failure,
loss and validation errors invalidate the session before releasing resources.
The platform window/canvas must stay alive until RHI shutdown.

The sole new lint suppression documents the integer-to-pointer conversion at the
C API userdata boundary: the value is an opaque numeric token and is never
interpreted as an address or dereferenced. Allocating permanent callback cookies
would instead retain memory until unknown late-callback lifetimes end. The
suppression applies only to that conversion, not engine call sites or headers.
JavaScript preflight catches DOM errors before the port's surface assertions.
Diagnostic messages honor explicit lengths or bounded NUL scanning for WGPU_STRLEN.

## Validation

- Development and Release web presets each pass 12 CTest cases, including RHI
  header self-sufficiency and real wasm execution through the pinned C API port.
- The Node-only probe provider exercises nine scenarios: success/restart cleanup,
  adapter failure with compatibility retry, device failure, surface validation
  failure, cancellation during adapter/device acquisition, stale completion after
  restart, device loss and uncaptured validation error. It verifies no optional
  requirements, Ready only after configuration/limits, and destruction/unconfigure
  counts. This controlled provider is included only by the probe, never the engine
  archive or browser navigator.gpu. It validates lifecycle logic, not GPU support.
- Native deterministic lifecycle tests cover nonblocking pending state, duplicate
  operations, stale tokens across restart, explicit failure reasons, loss and frame
  pairing. Native RHI tests also verify early invalid-window rejection.
- Native Development: 22 registered tests pass; ASan/UBSan: 20 pass. The Platform
  display test is skipped because there is no test display. The live Wayland/
  Vulkan Catch2 case is opt-in and was not exercised. One first-pass sanitizer
  diagnostic test missed its datagram under concurrent validation; its isolated
  retry and the final full sanitizer suite both pass.
- Both web format/tidy configurations pass. Native full format/tidy and targeted
  checks of the final Vulkan guard/tests pass. Installed native SDK consumer passes.
- Real in-app browser: adapter unavailable is explicitly reported. Shutdown
  transitions to Idle and restart reports adapter unavailable again without
  crashing; screenshot inspected. This environment cannot prove real-GPU Ready.
  No browser flags or security protections were changed.
- Web CI includes RHI paths and publishes both lifecycle probe configurations;
  hosted CI results are pending when the PR is opened.

## Reproduce and continue

```bash
./scripts/test web-emscripten-development
./scripts/check web-emscripten-development --all
./scripts/test web-emscripten-release
./scripts/check web-emscripten-release --all
./scripts/test linux-clang-development
./scripts/check linux-clang-development --all
./scripts/test linux-clang-asan-ubsan
./scripts/install-sdk linux-clang-development
python3 -m http.server 8767 --bind 127.0.0.1 --directory out/build
```

Open `/web-emscripten-development/tools/web-rhi-probe/` or its Release counterpart
on the user's WebGPU-capable browser and verify Ready with a nonzero texture limit,
then Shutdown/Restart. The empty canvas is intentional: W4 does not draw frames.
The probe shell fits the available embed viewport. Package index.html/index.js/
index.wasm at ZIP root for itch.io. Check this PR's merge/CI before branching for
W5 frame rendering. Keep W0 flag-free support and continuous-animation acceptance
open; neither controlled-provider tests nor unavailable-adapter handling closes
those hardware gates.
