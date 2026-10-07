# W0 WebGPU feasibility probe

This standalone tool checks the pinned browser C API before porting the engine.
It requests a core adapter, retries once with WebGPU compatibility mode if
unavailable, requests a device asynchronously, and clears a 640×360 canvas with
an animated color. It deliberately does not link the Linux-only engine runtime.
See [ADR 0009](../../decisions/0009-browser-webgpu-toolchain.md) and the
[implementation plan](../webgpu-implementation-plan.md).

## Prepare the tools

Use Linux with Python 3.10+, native CMake 3.29.6/Ninja, and clang-format/tidy 18.
`./init.sh --no-system-install --preset-only linux-clang-development` prepares
the native host tools. Emscripten is separately pinned; this probe does not
change the native reference compiler.

From the repository root, install the official SDK into generated state:

```bash
curl -fL https://github.com/emscripten-core/emsdk/archive/refs/tags/4.0.23.tar.gz -o /tmp/ludus-emsdk-4.0.23.tar.gz
echo 'a91a4c1f42dbb0345faac093161e27d43e9b6964840d8c8d80976ab8d3eaf2d3  /tmp/ludus-emsdk-4.0.23.tar.gz' | sha256sum --check
mkdir -p out/host-tools/emsdk
tar -xzf /tmp/ludus-emsdk-4.0.23.tar.gz --strip-components=1 -C out/host-tools/emsdk
out/host-tools/emsdk/emsdk install 4.0.23
out/host-tools/emsdk/emsdk activate 4.0.23
```

Verify the checksum before extraction; stop if any command fails. No shell
startup-file changes are needed. Downloads require network access. Keep an
existing SDK elsewhere and pass `--sdk /absolute/path/to/emsdk` to each command
if necessary. The build fetches the SDK-pinned, checksum-verified WebGPU port.

## Build, analyze, and package

```bash
./scripts/webgpu-probe build
./scripts/webgpu-probe check
./scripts/webgpu-probe package
./scripts/webgpu-probe build --configuration Release
./scripts/webgpu-probe check --configuration Release
./scripts/webgpu-probe package --configuration Release
```

The probe follows repository formatting, numeric types, warnings, and the
exception ban. `check` applies the repository's formatter to `main.cpp` and
clang-tidy 18 to the same wasm source with its target/sysroot/port headers.
For format fixes, use `format_source` from `scripts/python/formatting.py`, as
the ordinary engine formatting command does not traverse `tools/`.

Packages are written to `out/packages/webgpu-probe-{debug,release}.zip`, with
the root `index.html`, JS/wasm files, iframe harness, and license notices.
Packaging prints an artifact SHA256 for the handoff record. Release currently
retains runtime assertions to collect feasibility evidence; size optimization
and production shell policy belong to W7.

## Run and record evidence

The dedicated `WebGPU feasibility probe` workflow compiles, analyzes, and
packages this tool on relevant PRs and offers the Release ZIP as an artifact.
It performs no browser or real-GPU validation; engine browser CI belongs to W8.

Extract the Release ZIP into a new generated directory and serve the package:

```bash
mkdir -p out/webgpu-probe-extracted
python3 -m zipfile -e out/packages/webgpu-probe-release.zip out/webgpu-probe-extracted
python3 -m http.server 8765 --bind 127.0.0.1 --directory out/webgpu-probe-extracted
```

Visit `http://localhost:8765/index.html` and `http://localhost:8765/iframe.html`.
Do not use `file://`. HTTPS is required when testing on a non-localhost host.
For actual itch.io validation, upload the Release ZIP to an authorized draft
HTML5 project, select browser playback, and test its real embed. The local
sandboxed iframe only approximates hosting behavior.

The Diagnostics panel records browser version and messages from both adapter
attempts, device requests, device loss, and uncaptured errors. Compatibility
requests remain WebGPU and may return a core-capable adapter; the status records
the requested mode, not a detected graphics backend. Older browsers may ignore
the compatibility option. No software-adapter or browser-flag bypass is enabled.

Confirm the status becomes ready, the color changes, and the console contains
no WebGPU validation failures. The status element exposes `data-state` and
`data-frames` for test evidence; readiness alone does not prove frame submission.
Record browser/version, OS, GPU/backend, flags, source revision, artifact hash,
URL, screenshot, and console errors. Browser flags/software adapters may help
debugging but cannot satisfy the real-hardware acceptance gate.

Check an unsupported browser, a browser without an available adapter, device
request failure, and missing JS/wasm. The shell should show an error instead of
remaining on a blank canvas. Device loss/uncaptured errors stop animation and
instruct a reload. Resource recovery and repeated startup within one module
are outside this probe and must be implemented/tested in W4.

See [W0 evidence](../webgpu-w0-evidence.md) for performed
checks and remaining acceptance gates.
