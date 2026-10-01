# W5: WebGPU frames and rendering proof

## Implementation

W5 starts from main after PR #38 merged. The public RHI adds FrameTarget and
SetFrameTarget without exposing WebGPU handles. Existing synchronous/native
entry points retain their signatures; native SetFrameTarget explicitly returns
Unsupported. Rebuild SDK consumers for the additional exported symbol.

After Ready, the browser caller applies the negotiated device dimension limit
through Platform, reads its framebuffer snapshot, sets dimensions/clear color,
and begins a frame. Zero size skips acquisition. Changes reconfigure the surface;
the pinned port's preferred canvas format is used. A successful frame owns its
current texture, view, encoder, pass and command buffer, submits once and releases
all five handles. There is no wgpuSurfacePresent call: browser presentation occurs
around the RAF tick. An open frame prevents target changes and duplicate begin.

Timeout skips; outdated surfaces unconfigure and retry on the next nonzero
frame. Lost surfaces stop the session instead of entering a retry loop. The pinned Emdawnwebgpu implementation emits only SuccessOptimal or Error
from browser getCurrentTexture, so those other status branches cannot be induced
through its ordinary browser provider. Error stops rendering with an explicit
RenderingUnavailable state. Device loss/uncaptured validation invalidates the
session and cleans up; callers must explicitly shut down/restart.

The separate tools/web-frame-probe owns its WGSL shader and triangle pipeline.
It clears with an animated color and draws a triangle through a centered square
viewport, preserving geometry across aspect changes. Shader/pipeline validation
uses an asynchronous error scope. Shutdown invalidates the sample generation
before releasing its pipeline; delayed callbacks cannot restart drawing. Failed
shader setup reports failure and stops the RHI. RAF is used in browsers; only the
Node fixture installs its own RAF implementation.

The sample uses a private RHI interop header for borrowed device/format/active
pass handles. It is not installed, enters no public header, and is intentionally
limited to this smoke proof. A general resource/pipeline API is deferred.

## Validation and limits

- Development and Release web builds are warning-clean, with 13 registered tests
  each. W4 lifecycle coverage remains separate and unchanged.
- Eight W5 controlled-provider scenarios execute the real wasm/C API bridge:
  success/restart, surface validation failure, shader validation failure,
  acquisition failure, shutdown while pipeline validation is pending, resize/DPR
  plus hide/resume, device loss and uncaptured validation. They verify triangle
  commands, square viewport, submission, no hidden acquisition, and per-frame
  handles disappearing from the pinned port's read-only JS handle registry.
  These mocks do not validate WGSL on hardware or prove pixels on the GPU.
  The port allocates an internal texture wrapper before getCurrentTexture; if
  that JS call throws, it returns Error without returning that wrapper. Engine
  cleanup cannot release an unreturned port allocation. The failure path stops
  immediately rather than repeatedly acquiring; registry checks establish
  release of handles returned to the engine, not a full port heap-leak audit.
- Native lifecycle tests cover skipped-frame pairing, changing a target while
  encoding, and loss during a frame. Native full suite passes (22 registered,
  no failures, one no-display skip);
  ASan/UBSan passes (20 registered, no failures, the same skip). Both web
  format/tidy configurations and the installed SDK consumer pass; native full
  format/tidy also passes.
- Native Vulkan command recording/submission is unchanged. The no-display
  Platform test skips here; the opt-in live Wayland/Vulkan case is not exercised.
- Real in-app browser: adapter unavailable. Both 640 × 360 and 320 × 240 embed
  documents fit without scrolling/clipping; canvas sizes were 616 × 251 and
  296 × 118 respectively. Shutdown/Restart stays usable.
  Browser console contains the expected adapter-unavailable RHI warnings. The
  automation also logged a MutationObserver error with no app source attribution;
  it is not evidence of rendering success. Screenshot: /tmp/ludus-w5-browser.jpg.
- Hardware acceptance is pending: visible triangle/color animation, resize and
  fullscreen without WebGPU validation errors, and native live Vulkan rendering.
  The user's earlier W0 success does not establish W5 acceptance. Flag-free W0
  support remains open. No browser flags were changed.

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

Open /web-emscripten-release/tools/web-frame-probe/ in a WebGPU-capable browser.
Expect Ready, pipeline ready, increasing frame count, an orange triangle and
changing background. Resize, hide/resume, Shutdown and Restart; enter fullscreen
through the itch.io player and inspect the console for validation errors. Package
index.html/index.js/index.wasm at ZIP root for itch.io. A release ZIP is prepared
at out/packages/ludus-w5-frame-probe-release.zip. The Node provider is inactive in
browsers. Check this PR's CI/merge and record target-browser screenshot/console
before W6 integrated smoke application work.
