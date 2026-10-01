# Packaged browser CI tests

This test-only runner executes the exact extracted Release ZIP in pinned
Playwright 1.55.1 / its bundled Chromium with a forced SwiftShader adapter.
Node is pinned to 22.16.0 in CI and npm dependencies have a committed integrity
lock. None of these files or dependencies ship in the game ZIP.

The software-GPU job follows the compile/package job and downloads its ZIP.
It checks real WGSL rendering by reading orange triangle pixels and animated
clear pixels, rather than trusting only frame counters. It also tests keyboard/
pointer movement, blur, hidden canvas, browser freeze/resume, resize, DPR, a
sandboxed iframe, fullscreen, stop/restart and device destruction. Separate
contexts inject missing WebGPU and adapter/device rejection, delay startup for
cancellation, or abort asset requests. These contexts use a real DOM and the
production wasm; injected failures remain explicitly synthetic.

Existing Node provider tests cover pipeline cancellation, callback generations,
validation errors and ownership release without claiming pixels. This browser
runner adds compositor/shader/DOM coverage. It fails if SwiftShader cannot render;
there is no skip or automatic mock fallback for the pixel case.

The CI-only launch arguments are recorded in report.json. The forced adapter
follows [Chromium's SwiftShader Vulkan pixel-test configuration](https://chromium.googlesource.com/chromium/src/+/354553e304d2ac575cd02d5dcb0d1551a09eac6f%5E%21/).
Experimental CI flags never establish flag-free player compatibility and must
not be copied into a user-browser acceptance procedure.

Reproduce on a disposable Linux CI/test host after preparing the package:

```bash
python3 -m zipfile -e out/packages/ludus-web-smoke-release.zip out/browser-qa/extracted
cd tools/web-browser-tests
npm ci
npx playwright install --with-deps chromium
npm test
```

The runner accepts extracted-directory, ZIP and output-directory paths as its
three optional arguments. Its report records the ZIP hash, browser version,
adapter, launch flags, HTTP requests and individual results. PNG screenshots
and report.json go under out/browser-qa/results, uploaded even on CI failure.
No special isolation headers are served. Real hardware/itch.io acceptance is
separately tracked in docs/development/webgpu-hosted-acceptance.md.
