# W1 FoundationBase browser evidence

Date: 2026-09-30. Base revision: `6e4a48fa6b93` (merged W0 and its diagnostic
follow-up). Branch: `codex/web-foundation-w1`. This is the CPU-only Foundation
stage; the browser engine still lacks logging, profiling, window/input and RHI.

## Implementation and policy

- Root Development/Release web presets use W0's pinned Emscripten SDK, isolated
  output/install trees and a separate dependency path. No native Conan artifacts,
  Volk/Wayland, native helper, Catch2 or native child tests enter the web graph.
- `init.sh`, bootstrap, doctor, build, test and check route browser presets to
  `scripts/python/web_build.py`. Doctor verifies SDK/port pins and host tools;
  bootstrap downloads only checksum-verified official emsdk source and installs
  the pinned SDK/CMake/Ninja. Native orchestration remains its existing path.
- Config identifies Web/wasm32 explicitly. Foundation emergency output uses a
  bounded console bridge independent of Logging. JavaScript exceptions are
  caught at that bridge and converted to Failed; C++ uses no exceptions.
- Browser sockets/control endpoints are unsupported and fail without waiting.
  The wire codec is extracted unchanged into a shared implementation. Debugger
  detection is Unknown; enabled ASSERT is terminal, CHECK reports and returns,
  REQUIRE/FATAL always terminate, and explicit debug break traps. Browser
  dialogs are disabled. Native behavior and assertion policy version remain 2.
- Browser SDK variant names append `-web-wasm32`; the generated web package
  avoids a Volk dependency. Scripted SDK consumer installation remains native
  at this stage, and the native installed consumer was verified.
- Development linking clears inherited DWARF with `-g0`, then uses `-g2` to
  retain wasm function names without the pinned SDK's limited-optimization
  warning. No compiler warning suppression is added.
- One narrow clang-tidy 18 false-positive suppression is documented on the
  private extern fatal-packet declaration: Emscripten libc++ atomics are reported
  as dynamically initialized even with constinit. Its sole definition is
  compiler-enforced constinit; no initialization rule is relaxed.

## Executed validation

Commands run from the isolated worktree:

```bash
./scripts/doctor web-emscripten-development
./scripts/test web-emscripten-development
./scripts/test web-emscripten-release
./scripts/check web-emscripten-development --all
./scripts/check web-emscripten-release --all
./init.sh --no-system-install --preset-only linux-clang-development
./scripts/test linux-clang-development
./scripts/check linux-clang-development --all
./init.sh --no-system-install --preset-only linux-clang-asan-ubsan
./scripts/test linux-clang-asan-ubsan
./scripts/install-sdk linux-clang-development
```

- Web: both builds pass C++23, no-exception and warnings-as-errors compilation;
  four CTest gates pass in each flavor. Separate SDK Node/wasm instances verify
  normal exit, repeated CHECK with exactly-once condition evaluation and integer/
  float formatting, terminal enabled ASSERT/REQUIRE/FATAL/trap, disabled ASSERT
  with no condition evaluation, and containment of a deliberately failing
  JavaScript console. Compile policy, Base standalone headers and foundational
  include boundaries pass. Both format/tidy 18 checks pass.
- Native Development: 20 registered tests, zero failures, one Wayland display
  test skipped because no display is available. ASan/UBSan: 18 registered tests,
  zero failures, the same display limitation. Both native builds have
  warnings-as-errors enabled. Native format/tidy and installed SDK consumer pass.
- CLI rejects an unknown web preset clearly. Python files compile; the workflow
  YAML parses. Bootstrap's fresh network-install branch is based on the existing
  W0 checksum/install path; this session reused the already installed SDK rather
  than claiming a fresh-machine onboarding test.
- Local in-app Chromium 154 on Linux: Development normal/CHECK return status 0;
  enabled ASSERT/REQUIRE/FATAL stop with abort errors, and explicit trap stops
  with unreachable. Release ASSERT/CHECK return 0; REQUIRE/FATAL stop. CHECK
  console evidence contains two reports and the after marker; console-failure
  returns 0 in both flavors. These probes do
  not require a GPU or experimental flags.

Generated build logs were kept under `/tmp/ludus-w1-*`. CI also compiles/tests/
analyzes both web flavors and uploads diagnostic HTML/JS/wasm files; remote CI
status must be checked separately from these local results.

## Remaining gates and handoff

W1's Foundation scope is implemented and locally validated. W0's real itch.io
core-adapter render uses the user's Vulkan flag; continuous animation and
flag-free supported GPU/browser coverage remain open deployment evidence. This
Foundation probe is not a playable game or a finished itch.io export.

Next stage is W2: port logging to synchronous browser dispatch/console sink and
audit profiling clock/thread/file behavior, preserving native instrumentation
policy. Extend the web target graph only as those implementations are verified;
do not add native logger threads or diagnostic helper startup to wasm.
