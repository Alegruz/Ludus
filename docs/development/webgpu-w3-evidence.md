# W3 browser canvas and input evidence

Implemented 2026-10-01 on merged W2 (`e9a16deacb13`). This stage adds Platform;
the browser renderer and playable game are still pending.

## Ownership and behavior

The browser preset selects a private Emscripten/DOM backend. Public headers expose
copied input events/state and a borrowed WebCanvas selector, with no DOM or GPU
objects. Creation copies the selector and requires an existing canvas. Keep that
selector identifying the same canvas, and keep both canvas and platform window
alive until RHI teardown. Style the canvas content dimensions explicitly to avoid
intrinsic-size/DPR feedback. Creation rejects duplicate ownership, invalid
selectors, embedded NULs, and invalid framebuffer limits. There are at most 16
live windows; monotonically increasing callback tokens refuse wraparound.

HandleEvent refreshes attachment, CSS size, DPR, visibility and focus once per
animation frame. ResizeObserver and DOM callbacks also update state. Framebuffer
size is rounded CSS size times DPR, clamped per dimension to the configured
limit (default 4096, accepted range 1–32768); hidden/zero-size/removed canvases
have zero framebuffer dimensions. W5 must supply the actual GPU device limit
and suspend rendering while dimensions are zero. This stage performs no GPU
allocation and does not measure hardware limits.

Input snapshots and events belong to the window; queries copy them into caller
storage. The bounded 128-event queue drops new events when full, increments
DroppedEvents, and continues updating authoritative held state. Consumers must
resynchronize from the snapshot when that counter changes. Physical keys are
KeyboardEvent.code positions; Unknown does not index held keys. Text/IME input,
multitouch, pointer lock, workers and gamepad support are outside this stage.
Primary pointer coordinates and normalized wheel deltas use CSS pixels. Blur,
hidden page/canvas and removal clear held keys/buttons; pointer cancellation or
lost capture clears only buttons. Tab and modified browser shortcuts retain
their default actions. Unmodified recognized controls and wheel/pointer events
are consumed only for the focused canvas when CaptureBrowserInput is enabled.

Destruction invalidates the C++ token before disconnecting the observer and
removing listeners. Guarded queued callbacks cannot enter the destroyed window.
An added tabindex is restored without overwriting a changed value. Replacement
through WindowManager explicitly resets its existing output before constructing
the new window; this also avoids retaining old callbacks through UniquePtr's
existing assignment behavior. JavaScript failures are converted at private
foreign boundaries, with C++ remaining exception-free.

Native Wayland/headless selection remains intact. The standalone-header and
foundational-include discovery now includes the one-level Platform module layout;
previously only two-level module layouts were enumerated. A headless regression
executable validates native descriptors and unsupported browser operations
without requiring a Wayland display.

## Validation

- Development and Release web builds: ten CTest cases pass in each, including
  actual wasm execution under pinned SDK Node and standalone Platform headers.
- Both web format/clang-tidy 18 checks pass, including web-only implementation
  and probe translation units. One narrow easily-swappable-parameters suppression
  documents the fixed numeric JavaScript callback ABI; internal dispatch uses a
  typed packet. Existing W2 SDK compatibility suppressions are unchanged.
- The wasm scenario validates exclusive canvas ownership, DPR sizing, focus/blur,
  CSS resize, clamping/rejected zero limit, hidden dimensions, Tab defaults,
  overflow without stuck keys, wheel normalization, primary pointer cancellation,
  window blur, restart without duplicate key callbacks, and canvas removal.
  Node uses an explicit test-only DOM fixture at DPR 2; browser pages use their
  real DOM at DPR 1. The fixture is included only by the probe target.
- Both real browser automated pages report `W3 lifecycle checks passed`.
  Manual Development canvas click/W/release gives one keydown and zero held keys;
  outside input loses focus, and restart again gives one keydown. The canvas
  screenshot was inspected. Automated CSS resize is validated in the real DOM;
  the automation viewport override did not yield a separately confirmed resize.
- Native Development: 21 registered cases complete without failures. ASan/UBSan:
  19 registered cases complete without failures. Each skips the Wayland display
  case because this session has no display; this is not live Wayland validation.
- Native format/tidy passes, including a targeted recheck of the added headless
  test and changed manager. Installed SDK and consumer pass.
- CI adds Platform paths and uploads Development/Release platform probe files.
  Hosted CI results are pending when the PR is opened.

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

Open `/web-emscripten-development/tools/web-platform-probe/` for interactive
input, or append `?automated` for lifecycle scenarios. Use a fresh page after
rebuilding. The Release counterpart has the same platform behavior.

Next is W4: RHI split and asynchronous lifecycle contract. Check this PR's merge
status and branch from updated main before continuing. W0 flag-free GPU support
and continuous animation on the user's target browser remain open; W3's CPU/DOM
success does not close that hardware deployment gate.
