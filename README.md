# Ludus Game Engine

Modern C++ (C++23) game/visualization engine skeleton with a small, batteries‑included core, platform abstraction, an Editor app, and a minimal unit test runner. The build is CMake‑first with presets for MSVC, Clang, and GCC, optional sanitizers, and integrated tooling (clang‑tidy/format, cppcheck, mimalloc).

## Quick Start (Windows)

- Prerequisites: Visual Studio 2022 (Desktop C++), CMake 3.26+, Git. Ninja is optional but used by presets. LLVM tools (clang‑format/clang‑tidy) are auto‑installed by `init.bat` when missing.
- Configure & build (MSVC, Debug):

```powershell
cmake --preset ninja_msvc-debug
cmake --build --preset ninja_msvc-debug -t LudusEditor
```

- Run the editor: built binaries are in `build/bin/`:

```powershell
./build/bin/LudusEditor.exe --quick-exit
```

- Run tests: tests are built in `build/bin/`:

```powershell
cmake --build --preset ninja_msvc-debug -t LudusTests
./build/bin/LudusTests.exe
```

If LLVM tools aren’t found, CMake will invoke `init.bat` or prompt to install via Chocolatey. You can also run `tools/install-llvm.ps1` manually from an elevated PowerShell.

## Project Structure

- include/Ludus/...: Public headers for core, math, containers, platform and editor.
- src/Engine/Core: Core library `LudusCore` (shared). Utilities, math, containers, assertions, CLI.
- src/Engine/Platform: Platform library `LudusPlatform` (shared). Platform glue under `Windows/`, `Unix/`, `MacOs/`.
- src/Engine: Interface target `Ludus` that links `LudusCore` + `LudusPlatform`.
- src/Editor: Executable `LudusEditor` with platform entry points under `Editor/Platform/<Platform>/Main.cpp`.
- src/Tests: Console test runner `LudusTests` using the built‑in minimal unit test registry.
- build/bin/: Built executables and DLLs (out-of-source, clean separation from source).
- build/lib/: Built libraries.
- cmake/: Helper CMake scripts (format checks).
- tools/: Scripts (e.g., `install-llvm.ps1`).

## Build Options (CMake)

- `ENABLE_MIMALLOC=ON|OFF` (default ON): Use mimalloc and override malloc/free. Auto‑disabled with sanitizers.
- `ENABLE_SANITIZERS=ON|OFF` (default ON for Clang/GCC Debug/RelWithDebInfo): ASan/UBSan/LSan for non‑MSVC toolchains.
- `ENABLE_CLANG_TIDY=ON|OFF` (default ON): Enables clang-tidy if available; gracefully disables if not found.
- `ENABLE_CPPCHECK=ON|OFF`, `ENABLE_MSVC_ANALYZE=ON|OFF`: Optional static analyzers.

Useful targets when `clang-format` is available:

```powershell
cmake --build --preset ninja_msvc-debug -t format-check
cmake --build --preset ninja_msvc-debug -t format-fix
```

## Cross‑Platform Notes

- Linux: use `ninja_clang-*` or `ninja_gcc-*` presets. Sanitizers are enabled in Debug/RelWithDebInfo for Clang/GCC.
- macOS: use `ninja_clang-*`. Install LLVM with Homebrew if needed (`brew install llvm`).
- Windows AddressSanitizer via MSVC is disabled in this repo due to compatibility issues; prefer Clang on Windows if you need ASan.

## Start Here

- New to the repo? Read the step‑by‑step guide in [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md). It covers setup, builds, running the editor, tests, and where to go next.

## Documentation Map

- Start here: [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md)
- Build system philosophy & CI integration: [docs/BUILD_SYSTEM.md](docs/BUILD_SYSTEM.md)
- Memory model: [docs/MEMORY_ARCHITECTURE.md](docs/MEMORY_ARCHITECTURE.md)
- Leak detection overview: [docs/LEAK_DETECTION.md](docs/LEAK_DETECTION.md)
- Leak detection quick ref: [docs/LEAK_DETECTION_QUICK_REFERENCE.md](docs/LEAK_DETECTION_QUICK_REFERENCE.md)
- Mimalloc integration: [docs/MIMALLOC_INTEGRATION.md](docs/MIMALLOC_INTEGRATION.md)
- Static analysis: [docs/STATIC_ANALYSIS.md](docs/STATIC_ANALYSIS.md)
- Unit testing: [docs/UNIT_TESTING.md](docs/UNIT_TESTING.md)

## Troubleshooting

- clang‑tidy/format auto-install failed: CMake attempts install via Chocolatey (Windows), Homebrew (macOS), or apt (Linux). For manual installation, see [docs/BUILD_SYSTEM.md](docs/BUILD_SYSTEM.md).
- Missing Ninja: install via `choco install ninja` (Windows) or your package manager; or use a Visual Studio generator instead.
- Build artifacts location: All outputs go to `build/bin/` (executables), `build/lib/` (libraries). Source root stays clean.

## License

See [LICENSE](LICENSE).
