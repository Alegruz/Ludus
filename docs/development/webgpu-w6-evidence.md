# W6: Integrated browser smoke application

## Implementation and compatibility

W6 starts after PR #40 merged with both workflows successful. apps/smoke now
shares Application start/tick/shutdown and simulation code between native and web
executables. The native driver keeps diagnostic session setup, logging/profiling
startup and capture export. It runs the shared tick in a native event loop; the
browser driver uses RAF and keeps application state alive across callbacks.
Native Vulkan command recording/submission is unchanged. Native rendering failure
now ends the application with a failure code instead of repeatedly logging errors.

The browser waits for RHI Ready and private pipeline validation before drawing.
Its sample renderer owns WGSL and pipeline resources using W5 private interop;
no public RHI or installed SDK contract is added. Failure/loss cancels rendering,
releases sample resources, shuts down RHI and destroys the platform window in
that order. Restart creates a new session; Stop is idempotent. Pending callbacks
cannot mutate the restarted app or access destroyed window state.

Platform snapshots drive WASD/arrow movement and pointer repositioning. Pointer
coordinates use CSS dimensions and remain independent of framebuffer density.
A square viewport preserves the triangle and bounds it inside the canvas. The
shared simulation clamps delta to 100ms, ignores invalid/negative delta and
freezes while hidden. Platform clears held input on blur/hide. RAF callbacks
refresh timing even while waiting for graphics; no blocking browser loop exists.

The shell exposes loading, unsupported-WebGPU, failure and device-loss messages,
Restart/Stop and user-initiated fullscreen. Controls and status remain outside the
canvas. Instructions appear while playing. Frame counts and positions live only
in QA data attributes; implementation details do not appear in the player UI.
Status text is changed only when its message changes. Fullscreen errors use a
separate notice so the next frame cannot erase them. Startup script/wasm failure
reports a readable reload message; complete missing-asset tests belong to W7.

Production index.js is built for web only without a test provider. A separate
node-only smoke-test.js links the explicit DOM/GPU fixture and executes the same
application/renderer sources. CI uploads only index.* from both web flavors.

## Validation

- Both web builds are warning-clean and pass 14 registered tests. The W6 test
  runs 13 controlled-provider scenarios: success/restart, missing WebGPU, adapter,
  device, surface, shader and acquisition failures, cancellation during startup
  or pipeline validation, DPR resize, input/suspension/resume, device loss and
  uncaptured validation. Provider tests exercise the real wasm/C API callbacks.
- Integration checks verify triangle commands/submission, no live returned frame,
  shader or pipeline handles after stop/failure, listener/window detachment after Stop, bounded movement after a
  simulated 60-second gap, keyboard input, CSS pointer mapping and hide/resume.
  They do not establish hardware shader validity or rendered pixels.
  The Node fixture is an explicit link dependency, so editing it relinks the
  test executable; both web flavors were relinked for the final ownership checks.
- Native full suite passes: 23 registered tests, no failures, one no-display
  Platform skip. ASan/UBSan passes: 21 registered, no failures, the same skip.
  New shared simulation tests cover resumed time, hidden/blurred input, CSS/DPR
  pointer mapping and movement bounds. Native startup against an explicitly
  nonexistent Wayland display cleanly logs failure and exits 1 without a window.
- Native and both web format/tidy checks pass; final header changes receive
  focused native/sanitizer rebuilds/tests and app tidy checks. SDK API is unchanged.
- Real browser shell at 640 × 360 and 320 × 240 has no document overflow/clipping.
  Stop changes to Stopped; Restart returns a readable unsupported-WebGPU message.
  Fullscreen activation changes to Exit fullscreen and expands to 1280 × 720;
  exit returns to the embed. Screenshot evidence is saved at
  /tmp/ludus-w6-browser.jpg and /tmp/ludus-w6-fullscreen.jpg.
- This in-app browser still returns no GPU adapter. Console includes expected
  RHI/Core diagnostics for adapter unavailability. Browser automation also logs
  an unattributed MutationObserver error, recorded rather than treated as app
  rendering evidence. No browser flags were changed.

Hardware acceptance stays open: actual triangle animation, input movement,
background/resume and fullscreen rendering without validation errors on the
user's browser, plus live native Vulkan rendering. W4/W5 and W0 flag-free gates
are not closed by these controlled tests. The pinned port's unreturned texture
wrapper on a throwing acquisition path remains the W5 limitation; app-owned
resources stop/clean up, but this is not a full port heap-leak audit.

## Reproduce and continue

```bash
./scripts/test web-emscripten-development
./scripts/check web-emscripten-development --all
./scripts/test web-emscripten-release
./scripts/check web-emscripten-release --all
./scripts/test linux-clang-development
./scripts/check linux-clang-development --all
./scripts/test linux-clang-asan-ubsan
python3 -m http.server 8767 --bind 127.0.0.1 --directory out/build
```

Open /web-emscripten-release/apps/smoke/ over localhost in a WebGPU-capable browser.
Expect an orange triangle and gently changing background. Click the canvas and
hold WASD/arrows, click/drag, resize and enter/exit fullscreen. Background/resume,
Stop and Restart; inspect the console for errors and save a hardware screenshot.

The temporary test ZIP out/packages/ludus-w6-smoke-release.zip contains
index.html/index.js/index.wasm at root. It is for target-browser testing; W7 will
add a reproducible packaging command, notices and clean-extraction validation.
Check W6 PR/CI/merge before starting W7. Retain the hardware gates if no actual
GPU screenshot/console evidence has been supplied.
