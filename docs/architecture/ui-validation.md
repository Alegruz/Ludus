# Runtime UI validation

Implementation: U0, `Ludus::Ui`. Full renderer/text/IME/accessibility and authoring
remain U1-U4; see [architecture](ui.md). Validation uses isolated clones so it
does not modify concurrent user work in either checkout.

## CPU measurement

A host microbenchmark submits 16, 128 and 1024 solid buttons, resolves a 1280 x
1536 logical viewport, then routes focus-next, activation, pointer-down and
pointer-up. It warms up 100 iterations and records 2000 samples. Clang 18.1.3,
C++23, Development core (`-O2`, assertions enabled), ordinary Linux host under
concurrent build load; no target hardware or total-frame claim.

| Elements | Median | p95 |
| --- | --- | --- |
| 16 | 1.73 us | 2.35 us |
| 128 | 13.53 us | 16.23 us |
| 1024 | 110.00 us | 135.53 us |

The source is `modules/ui/tests/benchmark.cpp`. Reproduce against an installed
Development SDK (paths below are local build outputs, not committed presets):

```sh
out/host-tools/bin/clang++ -std=c++23 -O2 \
  -I out/install/linux-clang-development/include \
  modules/ui/tests/benchmark.cpp \
  out/install/linux-clang-development/lib/libludus_ui.a \
  out/install/linux-clang-development/lib/libludus_foundation_containers.a \
  out/install/linux-clang-development/lib/libludus_foundation_base.a \
  -ldl -pthread -o /tmp/ludus-ui-benchmark
/tmp/ludus-ui-benchmark
```

These figures
measure CPU resolution plus four events, not font shaping, GPU composition,
script execution or platform input. Workload contains no text, image or modal
controls. Do not extrapolate to AAA screens.

The separate allocation regression verifies zero new/delete calls across 1000
64-element document/event iterations after initialization. It also injects
failure at each of ten initialization allocations and verifies successful retry.
Sanitizer builds run the ordinary behavior tests; replacement allocators are
kept in a separate non-sanitized test executable.

## Verification record

Local verification with the pinned toolchains:

- Warning-clean Development engine build and web Development build, including
  browser smoke apps and the installed Ui archive/header.
- Full Development CTest: 49 tests, zero failures; two live Wayland tests skipped
  because there is no compositor. Debug Ui behavior/allocation tests passed.
- Ui ASan/UBSan behavior tests passed, including LeakSanitizer. The host sandbox
  prohibits ptrace and Unix-socket IPC, so these and the existing IPC tests ran
  outside it. No physical GPU/runtime-native window acceptance is claimed.
- Project formatting and foundational include checks passed. Full clang-tidy
  analysis found one swappable test-helper parameter pair; after using a typed
  parent index, the affected translation unit passed re-analysis. All other
  translation units passed the full analysis run.
- Installed native SDK consumer compiled, linked and ran. Native and web
  consumer configure checks accepted Ui, tolerated an unknown optional
  component and rejected an unknown required component with a precise message.
- Sandbox native build and all four CTests passed. Its repaired local setup
  exposed selectable configure/build/test presets and configured native and
  both web profiles. Web Development rendered matching HUD pixels on WebGPU
  and WebGL 2 at 960 x 540 (1x) and 390 x 844 (2x); screenshots were inspected.

The PR checks supply full Debug/sanitizer, optional-editor, PCH, build-budget
and packaged Release browser validation on CI. Browser screenshots exercise
Chromium SwiftShader; software GPU checks do not establish physical GPU/mobile
behavior or interactive frame-rate performance.
