# Project live reload — evidence ledger

Status tracker for the `project-live-reload` implementation (spec under
`.kiro/specs/project-live-reload/`). Records exact revisions, tools, commands and
results, distinguishing **PASS / FAIL / SKIP / UNAVAILABLE** honestly. A green
workflow summary that contains a failed `continue-on-error` step is NOT a PASS.

## Environment (recorded host)

| Item | Value |
| --- | --- |
| OS | Amazon Linux 2023 (`6.1.x`, glibc 2.34), x86-64 |
| Compiler | Clang 18.1.8 (AWS build), `-std=c++23`, `-fno-exceptions` |
| Linker | lld 18.1.8 |
| Format/analysis | clang-format 18.1.8, clang-tidy 18.1.8 |
| Build | CMake 3.29.6, Ninja 1.11.1.3 (project-managed venv) |
| Packages | Conan 2.8.1 |
| Debugger | **LLDB 18.1.8 (available)**; GDB / RAD **UNAVAILABLE** (RAD optional) |
| GPU / display | **UNAVAILABLE** — no `/dev/dri`, no X/Wayland display, no Xvfb, no libvulkan |

### Standing blockers

- **Rendered on-screen frame (L01/L14/L15/L6):** the RHI ships only a real
  Vulkan backend; with no GPU/display/Vulkan loader in this sandbox a real
  windowed, rendered frame cannot be produced. The host drives the full windowed
  lifecycle to the capability-error boundary and the render path is exercised
  headlessly. Marked **UNAVAILABLE (hardware)**; a headless/offscreen run is not
  claimed as proof of rendered-window behavior.

## Phase ledger

| Phase/requirement | Revision and command | Result | Evidence/blocker |
| --- | --- | --- | --- |
| L0 / L04 ABI layout | `ludus_runtime_game_api_tests` | **PASS** | 58 assertions; standard-layout POD, measured size/align/offsets on x86-64. |
| L0 / L04 header budgets & self-sufficiency | `ludus_runtime_game_api_header_check`, `scripts/check --format` foundational-includes | **PASS** | Each public ABI header compiles standalone; foundational include boundary OK. |
| L0 two-generation load | `ludus_game_host_loader_tests` (Debug) | **PASS** | 8 cases/25 assertions: A+B coexist, distinct per-variant code, hidden symbol scope, missing-entry/wrong-identity rejected before Create, non-absolute path rejected, headless mandatory lifecycle. |
| L0 under ASan/UBSan | `ludus_game_host_loader_tests` (asan-ubsan, `ASAN_OPTIONS=detect_leaks=1`) | **PASS** | All pass, no leaks reported. |
| L0 clang-tidy-18 | `clang-tidy-18 --warnings-as-errors='*'` on runtime sources | **PASS** | Clean. Intentional fixed-width ABI enums carry `NOLINT(performance-enum-size)`. |
| L1–L6 | Pending | Not run | Implementation in progress. |

## Usage (so far)

```bash
# Build + run the ABI and loader acceptance (pinned toolchain)
./scripts/build linux-clang-debug
./out/build/linux-clang-debug/modules/runtime/game_api/ludus_runtime_game_api_tests
./out/build/linux-clang-debug/modules/runtime/game_host/ludus_game_host_loader_tests
```
