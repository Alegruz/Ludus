# Getting Started

This guide gets you from clone → build → run in minutes, then orients you to the codebase and deeper docs.

## 1) Prerequisites

- Windows: Visual Studio 2022 (Desktop development with C++), CMake 3.26+, Git. Ninja recommended. LLVM tools (clang-format/clang-tidy) will be auto-installed if missing.
- Linux: Clang or GCC toolchain, CMake 3.26+, Ninja recommended.
- macOS: Xcode Command Line Tools, Homebrew for `llvm` if needed, CMake 3.26+, Ninja recommended.

If tools are missing, the top-level CMake runs `init.bat` (Windows) or uses your package manager to install LLVM tools.

## 2) Configure and Build

Windows + MSVC Debug (recommended to start):

```powershell
cmake --preset ninja_msvc-debug
cmake --build --preset ninja_msvc-debug -t LudusEditor
```

- The build emits shared libraries to `build/lib/` and copies runtime DLLs and `LudusEditor.exe` to the repo root for easy launching.

Alternative presets:

- Clang (Linux/macOS/Windows): `ninja_clang-debug`, `ninja_clang-release`, `ninja_clang-relwithdebinfo`
- GCC (Linux): `ninja_gcc-debug`, `ninja_gcc-release`, `ninja_gcc-relwithdebinfo`
- Tests: `test_msvc`, `test_clang`, `test_gcc` configure like Debug and define test flags

## 3) Run the Editor

```powershell
./LudusEditor.exe --quick-exit
```

- Windows entry point: `src/Editor/Platform/Windows/Main.cpp`
- The sample shows command line parsing, math utilities, window creation, and a message pump.

## 4) Run the Tests

```powershell
cmake --build --preset ninja_msvc-debug -t LudusTests
./build/bin/LudusTests.exe
```

- Test main: `src/Tests/Main.cpp`
- Example tests live alongside core: `src/Engine/Core/CoreTests.cpp`
- The test registry reports failures via `main`’s exit code.

## 5) Formatting and Analysis

- Formatting:

```powershell
cmake --build --preset ninja_msvc-debug -t format-check
cmake --build --preset ninja_msvc-debug -t format-fix
```

- clang-tidy is enabled by default when available and treats warnings as errors in CMake.
- Optional analyzers: enable `ENABLE_CPPCHECK=ON` or `ENABLE_MSVC_ANALYZE=ON` in your cache.

## 6) Build Options

- `ENABLE_MIMALLOC=ON|OFF` (default ON): Overrides malloc/free for perf profiling. Auto-disabled when sanitizers are on.
- `ENABLE_SANITIZERS=ON|OFF` (default ON for Clang/GCC in Debug/RelWithDebInfo): Adds ASan/UBSan/LSan. MSVC ASan is disabled here due to Windows compatibility; use Clang on Windows if you need ASan.
- `ENABLE_CLANG_TIDY=ON|OFF` (default ON): Enables clang-tidy (`-warnings-as-errors=*`).

You can set options at configure time, for example:

```powershell
cmake --preset ninja_clang-relwithdebinfo -DENABLE_MIMALLOC=OFF
```

## Architecture Map

- `LudusCore` (shared lib): Core utilities, containers, math, assertions, command line helpers.
  - Headers: `include/Ludus/Engine/Core/...`
  - Sources: `src/Engine/Core/*`
- `LudusPlatform` (shared lib): Platform abstraction layer.
  - Headers: `include/Ludus/Engine/Platform/...`
  - Platform-specific sources under `src/Engine/Platform/<Platform>/` (e.g., `Windows/Common.cpp`)
- `Ludus` (interface lib): Convenience umbrella target that links `LudusCore` + `LudusPlatform`.
- `LudusEditor` (exe): Editor app with platform entry points under `src/Editor/Platform/<Platform>/Main.cpp`.
- `LudusTests` (exe): Minimal console test runner using an in-house registry.

## Source & Include Layout

- `include/` contains the public API headers consumed by all targets.
- `src/Engine/Core/` implements core runtime support.
- `src/Engine/Platform/` provides platform glue; add new platforms here and wire up `PLATFORM_FOLDER` in CMake.
- `src/Editor/` hosts the Editor app and its platform entry points.
- `src/Tests/` hosts test main and test files.

## Common Workflows

- Add a new unit test: place it in `src/Engine/Core/CoreTests.cpp` or add a new file under `src/Engine/Core/`, register via the test registry API and rebuild `LudusTests`.
- Add a new platform: create `src/Engine/Platform/<YourPlatform>/Common.cpp` and corresponding headers under `include/Ludus/Engine/Platform/<YourPlatform>/`, then ensure `PLATFORM_FOLDER` maps correctly in top-level CMake (it’s auto-set by platform ID).
- Debug memory/leaks: see [docs/LEAK_DETECTION.md](LEAK_DETECTION.md) and use `LUDUS_LEAK_DETECTOR()` scopes as shown in `src/Editor/Platform/Windows/Main.cpp` and `src/Tests/Main.cpp`.

## Where to Read Next

- Memory architecture: [docs/MEMORY_ARCHITECTURE.md](MEMORY_ARCHITECTURE.md)
- Leak detection overview: [docs/LEAK_DETECTION.md](LEAK_DETECTION.md)
- Quick leak detection usage: [docs/LEAK_DETECTION_QUICK_REFERENCE.md](LEAK_DETECTION_QUICK_REFERENCE.md)
- Mimalloc integration: [docs/MIMALLOC_INTEGRATION.md](MIMALLOC_INTEGRATION.md)
- Static analysis pipeline: [docs/STATIC_ANALYSIS.md](STATIC_ANALYSIS.md)
- Unit testing: [docs/UNIT_TESTING.md](UNIT_TESTING.md)

## Troubleshooting

- LLVM tools missing: run `init.bat` or `tools/install-llvm.ps1` in an elevated PowerShell, then `cmake --preset ...` again.
- Sanitizers on Windows: prefer Clang presets (`ninja_clang-*`). MSVC ASan is disabled here.
- Mimalloc conflicts with sanitizers: it auto-disables when sanitizers are ON.
- Editor exe location: `LudusEditor.exe` and runtime DLLs are copied to repo root after build.
