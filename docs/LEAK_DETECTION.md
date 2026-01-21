# Memory Leak Detection Guide

This document outlines the memory leak detection strategies for the Ludus project across different platforms and configurations.

## Overview

Memory leak detection is enabled by default in **Debug** and **RelWithDebInfo** builds to catch issues early with the container code that uses manual `malloc`/`free` and `new`/`delete` operations.

---

## Linux/macOS: AddressSanitizer (ASan) + LeakSanitizer (LSan)

### Default Behavior

- **Enabled by default** in Debug and RelWithDebInfo builds (non-MSVC)
- Detects: heap overflows, use-after-free, leaks, uninitialized reads
- Flags: `-fsanitize=address,undefined,leak -fno-omit-frame-pointer -g`

### Usage

Simply build with a Debug preset:

```bash
cmake --preset ninja_gcc-debug
cmake --build --preset ninja_gcc-debug
./out/build/ninja_gcc-debug/src/Editor/LudusEditor
```

Leak reports appear in stderr on program exit.

### RelWithDebInfo (Optimized Leak Detection)

For testing leaks in optimized builds:

```bash
cmake --preset ninja_gcc-relwithdebinfo
cmake --build --preset ninja_gcc-relwithdebinfo
./out/build/ninja_gcc-relwithdebinfo/src/Editor/LudusEditor
```

---

## Linux: Valgrind Memcheck

### When to Use

- Catches leaks ASan might miss (shutdown-only, edge cases)
- Independent verification of heap correctness
- Slower than ASan but more thorough

### Manual Usage

```bash
# After building with Debug preset
valgrind --leak-check=full --show-leak-kinds=all \
  ./out/build/ninja_gcc-debug/src/Editor/LudusEditor
```

### CI Integration

The CI workflow automatically runs Valgrind on all Linux Debug builds (Clang and GCC).
Reports are uploaded as artifacts.

### Key Valgrind Flags

- `--leak-check=full` - Full memory leak detection
- `--show-leak-kinds=all` - Report all leak categories
- `--track-origins=yes` - Track where uninitialized values originated
- `--log-file=<file>` - Output to file (useful in CI)

---

## Windows: CRT Leak Detection

### Default Behavior (Debug Mode)

The Windows editor (`src/Editor/Platform/Windows/Main.cpp`) automatically enables CRT leak detection in Debug builds:

```cpp
#if defined(LUDUS_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    // ... program runs ...
    _CrtDumpMemoryLeaks();  // Called on exit
#endif
```

Leak reports appear in the **Debug Output** window in Visual Studio.

### Using Visual Studio's Built-in Tools

#### 1. Heap Snapshots (Diagnostics Hub)

- Build in Debug mode
- Run: **Debug** → **Performance Profiler**
- Select **Memory Usage**
- Click **Take Snapshot** at start and end of operations
- Compare snapshots to find retained allocations

#### 2. ASan (Limited)

MSVC's ASan support (`/fsanitize=address`) is enabled in Debug builds:
- Catches: heap buffer overflows, use-after-free, some leaks
- **Limitations**: No LSan equivalent, less comprehensive than GCC/Clang

### Disable CRT Leak Detection (if needed)

Edit [src/Editor/Platform/Windows/Main.cpp](../../src/Editor/Platform/Windows/Main.cpp):

```cpp
// Comment out or remove in release/profile builds:
// _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
```

---

## Advanced: Dr. Memory (Optional)

**Dr. Memory** is a runtime memory debugging tool similar to Valgrind for Windows.

### Installation

```bash
# Windows (via Chocolatey)
choco install drmemory

# Or download from: https://github.com/DynamoRIO/drmemory
```

### Usage

```bash
drmemory -leaks_only -- .\out\build\ninja_msvc-debug\src\Editor\LudusEditor.exe
```

### CI Integration (Optional)

To add Dr. Memory to the Windows CI pipeline, add a step after the build:

```yaml
- name: Run Dr. Memory Memcheck (Windows)
  if: runner.os == 'Windows'
  run: |
    choco install drmemory --no-progress
    $exe = Get-ChildItem -Recurse -Filter "LudusEditor.exe" | Select-Object -First 1
    drmemory -leaks_only -batch -- $exe.FullName
```

---

## CI/CD Integration

### Current Coverage

| Platform | Tool           | Status     | Notes                           |
|----------|----------------|------------|--------------------------------|
| Linux    | ASan/LSan      | Automatic  | Debug builds via CMake          |
| Linux    | Valgrind       | Automatic  | CI job: `valgrind-memcheck`     |
| macOS    | ASan/LSan      | Automatic  | Debug builds via CMake          |
| Windows  | CRT Leak Check | Automatic  | Debug builds in Visual Studio   |
| Windows  | MSVC ASan      | Automatic  | Debug builds via CMake          |
| Windows  | Dr. Memory     | Manual     | Optional: install & run locally |

### Running CI Checks Locally

```bash
# Linux: ASan (via Debug preset)
cmake --preset ninja_gcc-debug && cmake --build --preset ninja_gcc-debug

# Linux: Valgrind (after building)
valgrind --leak-check=full ./out/build/ninja_gcc-debug/src/Editor/LudusEditor

# Windows: CRT (automatic on Debug exit)
cmake --preset ninja_msvc-debug && cmake --build --preset ninja_msvc-debug
# Run the executable and check Visual Studio Debug Output
```

---

## Container Code: Key Allocations to Monitor

The following areas use manual memory management:

- [Array.hpp](../../include/Ludus/Engine/Core/Container/Array.hpp) - Dynamic resizing with `malloc`/`realloc`
- [String.cpp](../../src/Engine/Core/Container/String.cpp) - UTF-8/UTF-16 conversions with `new`
- [CommandLineManager](../../include/Ludus/Engine/Core/CommandLineManager.h) - Argument parsing

All of these are covered by sanitizers in Debug builds.

---

## Troubleshooting

### ASan Disables in Release

To force ASan in Release mode (testing only):

```bash
cmake --preset ninja_gcc-relwithdebinfo  # Uses RelWithDebInfo + ASan
```

### False Positives in Valgrind

Valgrind may report leaks from third-party libraries. Use a suppression file:

```bash
valgrind --gen-suppressions=all --log-file=output.txt ./program
# Edit output.txt, move suppression blocks to suppressions.supp
valgrind --suppressions=suppressions.supp ./program
```

### CRT Leak Detection Too Noisy

If CRT reports false positives:
1. Filter by allocation number: `_CrtSetBreakAlloc(n)`
2. Or disable temporarily during development
3. Use Visual Studio's Heap Profiler for targeted inspection

---

## References

- [ASan Documentation](https://github.com/google/sanitizers/wiki/AddressSanitizer)
- [LSan Documentation](https://github.com/google/sanitizers/wiki/AddressSanitizerLeakSanitizer)
- [Valgrind User Manual](https://valgrind.org/docs/manual/)
- [MSVC CRT Debugging](https://docs.microsoft.com/en-us/cpp/c-runtime-library/debug-heap-details)
- [Dr. Memory](https://github.com/DynamoRIO/drmemory)
