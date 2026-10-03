# WebGL 2 fallback and Drift handoff

The recovered Kiro implementation is integrated with current main and hardened.
The engine has Auto → WebGPU → WebGL 2 startup selection in one artifact. Drift
consumes its installed public SDK and generates every backend from one Slang
source. Native Vulkan remains the default for native builds.

## Build and consume

On the pinned Ubuntu 24.04 x86_64 reference host:

```bash
./init.sh web-emscripten-release --preset-only
./scripts/build web-emscripten-release
out/host-tools/venv/bin/cmake --install out/build/web-emscripten-release \
  --prefix out/install/web-emscripten-release
./scripts/package-web
python3 -m zipfile -e out/packages/ludus-web-smoke-release.zip out/browser-qa/extracted
cd tools/web-browser-tests
npm ci
npx playwright install --with-deps chromium
npm test
```

Initialization explicitly acquires pinned Slang 2026.1.2, the digest-pinned
SPIR-V validator, and SPIRV-Cross `vulkan-sdk-1.4.313.0`. Normal shader builds
remain offline. `scripts/bootstrap-spirv-cross` verifies the source archive,
builds with Clang 18, and writes a source-identity/binary-hash sidecar. The
installed shader helper verifies the sidecar before executing that host tool.

A consumer uses `find_package(Ludus CONFIG REQUIRED)`, links
`Ludus::GraphicsRhi`, and calls `ludus_compile_shader`. For Emscripten, include
the installed prefix in both `CMAKE_PREFIX_PATH` and `CMAKE_FIND_ROOT_PATH`.
Set `LUDUS_SLANG_COMPILER`, `LUDUS_SPIRV_VALIDATOR`, and `LUDUS_SPIRV_CROSS`.
The last tool is required on web, optional natively. No private headers or
vendored engine sources belong in the game.

## Rendering and lifecycle contract

`Start(app, window)` uses Auto. The additive overload takes
`BackendSelection::Auto`, `WebGPU`, or `WebGL2`. Forced selection fails
explicitly; it never silently switches. `StartupInfo` contains the selected
backend and bounded attempt diagnostics. Capture it before `Shutdown()`.

Auto makes one WebGPU attempt (including its Core/Compatibility retry), then
one WebGL 2 attempt for capability/startup failure. Validation defects and
failures after Ready are not hidden by fallback. Each attempt uses a fresh
generation; stale callbacks cannot destroy a newer session. Platform replaces
and rebinds the managed canvas for API changes or restart. A failed replacement
finalizes the WebGL attempt without recursively retrying.

The fullscreen slice supports generated GLSL ES 3.00 and one std140 uniform
block at binding 0, 16..4096 bytes, multiple of 16. GLSL member offsets are
independently derived and compared with Slang's SPIR-V/WGSL reflection. Arrays,
matrices and other unsupported block fields fail explicitly. WebGL validates
the real linked block size. Resources and uploads are bounded and reused.

Author vertex IDs with `SV_VulkanVertexID`; use `LUDUS_GLSL_ES` in the same
source to normalize fragment Y coordinates using resolution. The helper emits
validated GLSL-specific SPIR-V and uses `--fixup-clipspace`; it does not rewrite
generated GLSL. Smoke's upload is 48 bytes; Drift's is 144 bytes.

## Current evidence

`webgl-fallback-validation.json` records current checks, browser case results,
package hashes and test-only launch flags. The three recovered
`webgl*-evidence.json` reports are historical Kiro evidence and are labeled as
such; their ZIP hash is not the current package identity.

The smoke suite exercises actual WGSL/GLSL compilation, presented pixels,
horizontal/vertical input, startup failures, forced policies, failed canvas
replacement, cancellation, context loss, resize/restart, DPR/iframe and missing
assets. Native checks cover unit tests, Vulkan pixels, ASan/UBSan, format/tidy,
header boundaries and an installed SDK consumer. The installed shader contract
checks no-op/source/include/option rebuilds and invalid tool/binding rejection.

Drift's separate repository fixes its asynchronous shader wait, retains attempt
diagnostics, uses Auto, regenerates the one ocean shader, and separates status
and controls in a responsive grid. Its browser tests compare frozen ocean
pixels across backends, exercise controls/pause/restart and loss, and test
narrow errors and an ordinary sandbox iframe.

## Remaining environment acceptance

Browser evidence uses pinned Chromium 140.0.7339.186 and SwiftShader. Test-only
software-GPU flags do not prove flag-free hardware compatibility. Drift's
expensive shader uses a recorded 100 ms test-only RAF delay to bound the
software GPU queue; this is not a frame-rate measurement. Physical GPU,
interactive performance and a hosted HTTPS/itch.io deployment still need
acceptance in those environments. The supplied Release ZIP can be uploaded
without rebuilding; no upload or publication was performed.
