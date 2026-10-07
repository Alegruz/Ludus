# W1 browser FoundationBase probe

This probe links the real FoundationBase library and exercises its browser
diagnostics. Enable the optional browser probes explicitly when preparing the
root web presets. Those presets also build the currently supported engine modules;
the narrower original W1 graph is recorded in [W1 evidence](../webgpu-w1-evidence.md).
Commands below run from the Ludus checkout root.

```bash
./init.sh --cli web-emscripten-development --preset-only --with-web-probes
./scripts/doctor web-emscripten-development
./scripts/build web-emscripten-development
./scripts/test web-emscripten-development
./scripts/check web-emscripten-development --all
./init.sh --cli web-emscripten-release --preset-only --with-web-probes
./scripts/build web-emscripten-release
./scripts/test web-emscripten-release
./scripts/check web-emscripten-release --all
```

Web init/bootstrap installs pinned CMake/Ninja in generated host tools, verifies
the official emsdk source checksum before extraction, installs/activates the W0
SDK pin, and configures only the chosen browser preset. It never edits shell
startup files or installs system packages. Python venv support and
clang-format/tidy 18 must already be available; native `./init.sh` can prepare
those prerequisites. `./scripts/bootstrap web-emscripten-development` also works.
Web init's all-presets/full-validation/CI modes are intentionally rejected;
run the explicit build/test/check commands above. For current browser SDK/package support and its separate acceptance, see
[browser packaging](../web-packaging.md).

CTest runs separate wasm instances through the SDK's Node runtime. It checks
reporting/termination, condition evaluation, repeated CHECK ownership release,
integer/float assertion formatting, unsupported native transport behavior, and
console bridge failure containment. It also runs the effective exception-policy
gate, foundational include gate, and standalone header gate for the exported
Base headers. It does not claim browser/GPU coverage from Node tests.

Serve the generated files to verify the browser boundary:

```bash
python3 -m http.server 8765 --bind 127.0.0.1 --directory out/build
```

Visit `/web-emscripten-development/tools/web-foundation-probe/index.html` and
the corresponding Release path. Each link starts a fresh module. Normal,
CHECK, and console-failure probes exit with status 0; CHECK emits two reports
and an after marker in the browser console. Development ASSERT, REQUIRE, FATAL,
and explicit trap stop the module and never emit the after marker. Release
ASSERT does not evaluate its condition; REQUIRE/FATAL still terminate. A
JavaScript runtime error from an intentional abort/trap is expected evidence.
No WebGPU adapter is needed for these CPU-only probes.

Browser emergency output is bounded text through console.error, independent of
normal logging. JavaScript failures in that bridge are contained and return
Failed; no C++ exception handling is enabled. Console display/durability and
JavaScript allocation/latency are browser properties, not real-time guarantees.
Browser debugger attachment is Unknown: enabled ASSERT is terminal, CHECK
never deliberately breaks, and there is no dialog/continue-once control path.
Explicit LUDUS_DEBUG_BREAK uses a non-resumable wasm trap. Thread id 1 is logical
for this single-thread build, not a host OS identifier. No sockets, signals,
worker threads, or helper startup enter the browser target graph.

The CI WebGPU probe workflow also builds/checks/tests both Foundation flavors
and uploads their generated HTML/JS/wasm files. These are diagnostic artifacts,
not a playable game or production itch.io package. See
[W1 evidence](../webgpu-w1-evidence.md) and the
[implementation plan](../webgpu-implementation-plan.md).
