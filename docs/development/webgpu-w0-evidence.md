# W0 probe evidence and handoff

Date: 2026-09-30. Branch: `codex/webgpu-w0`. Base: `29436c9` (current remote
main when implementation began). Status: **in progress; hardware acceptance
is pending**. No browser engine backend or itch.io game has been released.

## Implemented

- Standalone exception-free C++23/Emscripten WebGPU C API probe with async
  adapter/device requests, animated clear submission, failure status, and
  per-frame handle release.
- SDK/port version and checksum lock, explicit web toolchain policy, ADR 0009.
- Build, format/tidy, and licensed ZIP package commands; sandboxed iframe
  harness and a dedicated compile/package workflow.
- Persistent stage plan and acceptance/handoff tracking for subsequent PRs.

## Toolchain observed

| Component | Version / identity |
| --- | --- |
| Emscripten | 4.0.23; compiler driver revision `7a5d93b50f6a3a35e85a0d2fc9e667b8498e6aed` |
| SDK build | `aaa43392544d695232b70eda706d751f18980c2a` |
| LLVM bundled with SDK | 22.0.0git; `5243501cca02d7e54294d4bb5de0a85b06c40b7d` |
| SDK libc++ | `_LIBCPP_VERSION=200100` |
| Emdawnwebgpu | `v20251002.162335`; downloaded ZIP SHA512 matches `config/web_toolchain.json` |
| Repository analysis | clang-format 18 and clang-tidy 18 |

The browser reached the adapter-result callback after the runtime feature
checks, demonstrating execution of `std::expected`, floating `std::to_chars`,
and `std::format` with production C++ exceptions disabled. This is a targeted
feature probe, not a claim that every engine dependency is portable.

## Commands and results

Commands below ran from the isolated worktree. The native source was unchanged.

| Check | Command | Result |
| --- | --- | --- |
| Web Debug compile | `./scripts/webgpu-probe build` | Pass; project warning set, `-Werror`, C++23, `-fno-exceptions` |
| Web Debug format/tidy | `./scripts/webgpu-probe check` | Pass with version 18 against the wasm source/sysroot/port headers |
| Web Release compile | `./scripts/webgpu-probe build --configuration Release` | Pass |
| Web Release format/tidy | `./scripts/webgpu-probe check --configuration Release` | Pass |
| ZIP packaging | `./scripts/webgpu-probe package` and `--configuration Release` | Pass; root HTML/JS/wasm, harness, and licenses inspected |
| Script syntax | `python3 -m py_compile scripts/webgpu-probe` | Pass |
| Workflow syntax | YAML parse and Python compile of setup heredoc | Pass; hosted workflow execution still pending |
| Native unit suite | `./scripts/test linux-clang-development` | 20 registered tests, zero failures; 1 Wayland display test skipped |
| Native format/tidy | `./scripts/check linux-clang-development --all` | Pass |
| Native sanitizers | `CI=true ./scripts/build linux-clang-asan-ubsan`; `./scripts/test linux-clang-asan-ubsan` | Pass; 18 registered tests, zero failures, 1 Wayland display test skipped |
| Installed SDK | `./scripts/install-sdk linux-clang-development` | Pass; external consumer links/runs and policy probe reports as expected |
| Diff hygiene | `git diff --check` | Pass |

Native warning-as-error validation uses explicit CMake configuration:

```bash
out/host-tools/venv/bin/cmake --preset linux-clang-development -DLUDUS_WARNINGS_AS_ERRORS=ON
out/host-tools/venv/bin/cmake --build --preset linux-clang-development --parallel 2
```

The development compile database contains `-Werror`. Do not pass a `-D` flag to
`scripts/build`: its extra arguments are build-command arguments, not configure
arguments. That attempted invocation rejected the flag and was replaced by the
explicit configure/build above.

Native socket/signal diagnostic tests and the SDK consumer initially failed
inside the restricted execution sandbox. They passed when rerun with those OS
facilities available. These restrictions were not worked around in engine code.
System/third-party tidy warnings remain filtered by repository settings; no
project warning suppression was added.

## Browser/package observations

Browser: Codex In-app Browser in this Linux session; its exact version was not
available through the browser inspection API. No experimental flags were added
by the agent. No `/dev/dri` GPU device is exposed to the shell, and the browser
adapter request returned no usable adapter. The separately listed Edge browser
was unavailable to the control tool. No real-GPU rendering result is claimed.

Release artifact: `out/packages/webgpu-probe-release.zip`, approximately 180 KiB.
SHA256 at this session's packaging:
`0061d74d9002bb84e3d0b7acdc7d2e7df0d1f2ff2e0e1b155b7d4d3203b94a0c`.
ZIP timestamps are not normalized; rebuilding/repackaging can change its hash.

| Scenario | Observation |
| --- | --- |
| Release app on localhost | Adapter callback displays “No WebGPU adapter is available”; `data-state=failed`, `data-frames=0`; no validation errors observed |
| Sandboxed iframe | Same readable adapter-unavailable result inside `allow-scripts allow-same-origin` harness |
| Extracted Release ZIP | HTML, JS, and wasm load from an independently extracted directory; same adapter callback result; no errors for that URL |
| Extracted iframe | Same result; demonstrates package paths work inside the local harness |
| JS missing | Shell displays “Could not load index.js. Check the package contents.” |
| wasm missing | Shell displays a request-failure message; console records wasm 404/instantiation failure as expected |

The missing-file cases used temporary fixture directories under `out/`, not
changes to the packaged artifact. Browser snapshots and status attributes were
inspected through the browser tool. No screenshot artifact or hosted HTTPS/
itch.io evidence was collected. The shell's absent-WebGPU branch and the device
request failure/loss branches are implemented but not yet runtime-validated.

## Next session

1. Check out the PR branch, read AGENTS.md and the plan, and build/extract the
   Release ZIP using `tools/webgpu-probe/README.md`.
2. Run it in a real GPU-enabled browser without experimental flags. Record
   browser version, OS, adapter/backend, increasing frame count, visible color
   animation, and console/screenshot evidence. Exercise the device failure/loss
   cases with a suitable test harness. If the browser exposes no adapter,
   diagnose platform support rather than marking the rendering test passed.
3. Complete HTTPS iframe and an authorized itch.io draft embed check, recording
   embed settings and the tested artifact hash. No itch.io upload was authorized
   or performed by this PR session; the Release package is ready for that test.
4. Resolve any CI/probe issues, then update W0 acceptance. W1 remains gated on
   real-device local feasibility. Actual itch.io acceptance is mandatory for W8
   even if hosted access is deferred while independent stages proceed.

Future RHI work must preserve callback lifetimes, avoid `wgpuSurfacePresent`,
and keep normal logging separate from FoundationBase failures. W0 startup
handles live until reload; W4 must implement explicit shutdown/cancellation.

## Compatibility retry follow-up

The probe now explicitly requests core WebGPU, retries once with compatibility
feature level on adapter failure, and shows bounded callback messages/statuses
and browser version in the page. Device loss and uncaptured errors also retain
the browser message. This does not establish a compatibility renderer policy for
the eventual engine; feature/limit negotiation remains later-stage work.

Debug/Release builds and format/tidy checks passed for this follow-up. Local
in-app Chromium 154 returned null for both adapter requests on the standalone
page and sandboxed iframe; both failures appeared in Diagnostics. Real-GPU
rendering and the actual itch.io embed remain pending with this updated package.

Temporary browser request stubs also verified exactly one core→compatibility
retry, no retry after core adapter success, and propagation of a controlled
device-request rejection plus the port's device-creation loss message. These
fixtures were generated outside source/package and do not prove GPU rendering.
Updated local Release ZIP SHA256:
`ceb6e6daaf4ed28bb22cfac01991c216c63194c44fc871c869fc7abb56a724bf`.

## User-hosted itch.io render, 2026-09-30

The user's updated screenshot shows the real itch.io draft embed reporting
`WebGPU is ready (core request)` with a rendered purple clear frame and no
visible WebGPU errors. It follows enabling Vulkan in Edge on the previously
reported Linux/Wayland Intel UHD 620 laptop. The preceding screenshot showed
null adapters in both core and compatibility mode. This is evidence of adapter,
device, surface and at least one submitted frame in the actual embed; a still
screenshot does not independently verify continuous animation. The successful
configuration uses an experimental Vulkan flag, so the flag-free W0/W8 support
gate remains open. There is no evidence requiring an external GPU.

The user then explicitly requested continuing implementation. Proceed with W1's
independent CPU-only Foundation port using the validated toolchain, while
retaining flag-free GPU support as an unresolved deployment gate.
