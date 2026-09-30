# ADR 0009: Browser WebGPU toolchain and feasibility probe

## Status

Applied to W0 and the W1 FoundationBase browser target. Compiler and package
validation do not establish flag-free real-device
or itch.io compatibility. See the implementation plan and probe evidence.

## Context

Ludus currently builds a Linux/Wayland/Vulkan engine. Its FoundationBase runtime
explicitly requires Linux. Browser adapter/device creation is asynchronous and
cannot be implemented by changing the native compiler or packaging its output.

The user selected WebGPU for browser rendering. Before changing engine APIs,
we need a reproducible C API probe and a tested compiler/dependency pair.

## Decision

Use the toolchain pinned in `config/web_toolchain.json`: Emscripten 4.0.23,
SDK build revision `aaa43392544d695232b70eda706d751f18980c2a`, and its vendored
Emdawnwebgpu `v20251002.162335` remote port. Apply
`--use-port=emdawnwebgpu:cpp_bindings=false` at compilation and final linking.
Use the C API rather than Dawn's less stable C++ wrapper.

The installed SDK reports LLVM `22.0.0git`, revision
`5243501cca02d7e54294d4bb5de0a85b06c40b7d`; its libc++ reports `_LIBCPP_VERSION`
`200100`. These are the SDK's components, not the native reference toolchain.
Keep native Clang/LLD 18 and repository clang-format/clang-tidy 18. A future SDK
upgrade is a separate change requiring probe, analysis, and runtime evidence.

The W0 probe is a standalone CMake project in `tools/webgpu-probe`. It includes
only Ludus's numeric aliases and does not link FoundationBase or bypass the
Linux runtime guard. It uses C++23, no extensions, `-fno-exceptions`, the project
warning set, and warnings-as-errors. It exercises `std::expected`, floating
`std::to_chars`, and `std::format` before requesting an adapter.

Use one browser thread without pthreads, Asyncify, JSPI, or a requirement for
cross-origin isolation. Adapter/device callbacks use `AllowSpontaneous`; their
state lives for the module lifetime. Frame callbacks return to the browser for
presentation. The port's `wgpuSurfacePresent` is unsupported; do not call it.

Only the probe uses direct DOM status reporting without the engine logger.
Normal engine diagnostics still use FoundationLogging; W1/W2 must implement
browser infrastructure without coupling assertions to it.

## Consequences

- Native presets, dependencies, public APIs, and installed SDK stay unchanged
  in W0. W1 introduces browser engine targets only after the local feasibility
  gate is satisfied.
- W0 owns no gameplay, shader pipeline, responsive window, or resource-recovery
  API. The fixed-size clear demo retains startup GPU handles until page unload;
  per-frame handles are released. W4 must provide explicit cleanup/cancellation.
- WebGPU capability and adapter/device failures are visible in the shell;
  unsupported platforms have no WebGL fallback. Actual supported combinations
  are determined by execution evidence, not by browser branding.
- The remote port verifies its archive with the pinned SHA512. The script
  rejects a different SDK version or remote-port version/checksum. Emscripten
  uses MIT/UIUC licensing; Dawn/Emdawnwebgpu includes BSD and Emscripten notices.
  The package includes the licenses supplied by the pinned SDK/port and Ludus.
- The standalone probe is outside the engine scripts' source traversal, so it
  has an explicit formatting/tidy command using the repository settings and
  actual wasm/port include paths. Do not count native tidy as web coverage.
- Localhost and an iframe harness are preliminary checks. Actual HTTPS/itch.io
  embedding and real-device rendering remain separate acceptance gates.

## W1 extension

The same SDK now compiles the actual FoundationBase target through isolated
root browser presets. No native Conan libraries, Vulkan/Volk, Wayland, logger,
profiler, or diagnostic helper enter that graph. Other modules remain native
until their stages. Foundation macros explicitly identify Web/wasm32. Browser
ASSERT dialogs are unavailable, debugger attachment is Unknown, and enabled
ASSERT/REQUIRE/FATAL terminate; CHECK reports and returns. Native behavior and
policy version 2 are unchanged. Browser SDK variants append `-web-wasm32`.

Emergency records use a bounded console bridge that contains JavaScript errors
and returns delivery status. The JavaScript catch is inside EM_JS; C++ remains
exception-free. This promises no JavaScript allocation/latency bound or durable
console storage. An explicit wasm debug break is non-resumable. The common
control wire codec is shared, but browser control sockets are unsupported.

Development probe linking uses `-g0 -g2` for named wasm functions without retaining
DWARF, avoiding the SDK's limited post-link optimization warning. Full engine
logging/profiling and lifecycle work are separate stages. See the W1 evidence.

## References

- [Pinned remote port and integrity checksum](https://github.com/emscripten-core/emscripten/blob/4.0.23/tools/ports/emdawnwebgpu.py)
- [Official browser C API integration](https://emscripten.org/docs/porting/multimedia_and_graphics/WebGPU-support.html)
- [Browser application lifetime and event loop](https://emscripten.org/docs/porting/emscripten-runtime-environment.html)
- [itch.io HTML5 packaging and embedding](https://itch.io/docs/creators/html5)
