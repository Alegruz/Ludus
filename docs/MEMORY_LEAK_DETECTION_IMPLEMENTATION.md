# Memory Leak Detection Implementation Summary

Status: draft
Owner: maintainers
Last updated: 2026-01-26


## Overview

This document summarizes the comprehensive memory leak detection infrastructure added to the Ludus project. Memory detection is critical for the codebase due to manual memory management in container classes (Array, String, etc.).

---

## Changes Made

### 1. Windows CRT Leak Detection (`LeakDetection.h`)

**File**: [include/Ludus/Engine/Core/LeakDetection.h](include/Ludus/Engine/Core/LeakDetection.h)

A new utility header providing platform-specific leak detection helpers:

```cpp
namespace ludus::core::debug
{
    void InitializeLeakDetection();      // Enable CRT leak detection in Debug
    void DumpMemoryLeaks();              // Dump leaks to debug output
    void SetBreakOnAllocation(long n);   // Break on specific allocation
    bool IsHeapValid();                  // Check heap integrity
}
```

**Key Features**:
- Only active on Windows in Debug builds (graceful no-op elsewhere)
- Wrapped CRT functions (`_CrtSetDbgFlag`, `_CrtDumpMemoryLeaks`, etc.)
- Safe, well-documented API
- Zero overhead in Release builds

### 2. Windows Editor Integration

**File**: [src/Editor/Platform/Windows/Main.cpp](src/Editor/Platform/Windows/Main.cpp)

Updated to automatically enable CRT leak detection:

```cpp
#if defined(LUDUS_DEBUG)
    ludus::core::debug::InitializeLeakDetection();
    // ... program runs ...
    ludus::core::debug::DumpMemoryLeaks();
#endif
```

**Effect**: Memory leaks are automatically reported to the Visual Studio Debug Output window on program exit when running in Debug mode.

### 3. Linux Valgrind Integration

**File**: [.github/workflows/cmake-multi-platform.yml](.github/workflows/cmake-multi-platform.yml)

Added new CI job `valgrind-memcheck` that:
- Installs Valgrind on Linux runners
- Runs both GCC and Clang Debug builds through Valgrind
- Checks for memory errors using `--leak-check=full --show-leak-kinds=all`
- Uploads reports as artifacts for inspection
- Fails the build if memory errors are detected

**Configuration**:
```bash
valgrind \
  --leak-check=full \
  --show-leak-kinds=all \
  --track-origins=yes \
  --verbose \
  --log-file=valgrind-report-*.txt
```

### 4. Optional Dr. Memory Support

**File**: [.github/workflows/cmake-multi-platform.yml](.github/workflows/cmake-multi-platform.yml)

Added commented-out `drmemory-memcheck` job for optional Windows memory testing. Can be enabled by uncommenting to add runtime memory analysis on Windows CI.

### 5. Comprehensive Documentation

**File**: [docs/LEAK_DETECTION.md](docs/LEAK_DETECTION.md)

Complete guide covering:
- Linux/macOS ASan/LSan configuration
- Linux Valgrind usage
- Windows CRT leak detection
- Visual Studio Diagnostics Tools (Heap Snapshots)
- Dr. Memory integration
- CI/CD coverage matrix
- Troubleshooting guide
- Container code areas to monitor

---

## Memory Leak Detection Coverage

| Platform | Build Type | Tool | Enabled | Status |
|----------|-----------|------|---------|--------|
| **Linux** | Debug | ASan/LSan | Default | ??Automatic |
| **Linux** | RelWithDebInfo | ASan/LSan | Default | ??New preset |
| **Linux** | Debug | Valgrind | CI only | ??New CI job |
| **macOS** | Debug | ASan/LSan | Default | ??Automatic |
| **macOS** | RelWithDebInfo | ASan/LSan | Default | ??New preset |
| **Windows** | Debug | CRT Leak Check | Default | ??Now integrated |
| **Windows** | Debug | MSVC ASan | Default | ??Automatic |
| **Windows** | RelWithDebInfo | MSVC ASan | Default | ??New preset |
| **Windows** | Debug | Dr. Memory | Optional | ?’¬ Commented in CI |

---

## Usage Guide

### For Developers

#### Windows Development
```powershell
# Build in Debug mode
cmake --preset ninja_msvc-debug
cmake --build --preset ninja_msvc-debug

# Run the editor
./out/build/ninja_msvc-debug/src/Editor/LudusEditor.exe

# Check Visual Studio Debug Output for memory leak reports on exit
```

#### Linux Development
```bash
# Build with ASan/LSan
cmake --preset ninja_gcc-debug
cmake --build --preset ninja_gcc-debug

# Run directly (sanitizers report to stderr)
./out/build/ninja_gcc-debug/src/Editor/LudusEditor

# Or use Valgrind for additional verification
valgrind --leak-check=full --show-leak-kinds=all \
  ./out/build/ninja_gcc-debug/src/Editor/LudusEditor
```

#### Optimized Leak Testing
```bash
# Test with optimizations enabled (new RelWithDebInfo presets)
cmake --preset ninja_gcc-relwithdebinfo
cmake --build --preset ninja_gcc-relwithdebinfo
./out/build/ninja_gcc-relwithdebinfo/src/Editor/LudusEditor
```

### Advanced: Using LeakDetection API

In custom Debug code, you can use the helper functions:

```cpp
#include <Ludus/Engine/Core/LeakDetection.h>

// In your code
ludus::core::debug::SetBreakOnAllocation(12345);  // Break on alloc #12345
if (!ludus::core::debug::IsHeapValid()) {
    // Heap corruption detected!
}
```

---

## Implementation Details

### CMakeLists.txt Changes

The root [CMakeLists.txt](CMakeLists.txt) was updated to:
- Enable sanitizers **by default** (not behind a flag) in Debug/RelWithDebInfo
- Add explicit LSan to Linux builds (`-fsanitize=leak`)
- Add frame pointers for better leak traces (`-fno-omit-frame-pointer`)
- Support both Debug and RelWithDebInfo configurations

### CMakePresets.json Changes

Added 6 new build presets:
- `ninja_clang-relwithdebinfo`
- `ninja_gcc-relwithdebinfo`
- `ninja_msvc-relwithdebinfo`

Each includes corresponding build presets for CI integration.

---

## Container Code Areas Covered

The following classes use manual memory management and are now covered:

1. **Array.hpp/Array.h** - Dynamic array resizing with `malloc`/`realloc`
2. **String.cpp/String.h** - UTF-8/UTF-16 conversions with `new`/`delete`
3. **CommandLineManager** - Argument parsing and storage

All allocations in these classes are now monitored by:
- ASan on GCC/Clang
- MSVC ASan on Windows
- CRT leak detection on Windows (Debug)
- Valgrind on Linux (CI)

---

## CI/CD Pipeline Impact

### Current Build Matrix

The GitHub Actions workflow now includes:

1. **Main build job** - 6 presets (Clang, GCC, MSVC on both Debug and Release)
2. **Valgrind job** - 2 configurations (Clang and GCC Debug with Valgrind)
3. **Optional Dr. Memory job** - Commented out, ready to enable

### Artifacts

Valgrind reports are automatically uploaded as GitHub Actions artifacts:
- `valgrind-reports-Clang`
- `valgrind-reports-GCC`

These are available for download on the Actions page.

---

## Troubleshooting

### False Positives in Valgrind

If third-party libraries generate false positives:

1. Run with `--gen-suppressions=all` to capture suppressions
2. Create a `suppressions.supp` file
3. Run with `--suppressions=suppressions.supp`

### CRT Reports Too Noisy (Windows)

If CRT leak detection is too verbose:

1. Use `_CrtSetBreakAlloc()` to focus on specific allocations
2. Or temporarily disable by commenting out `InitializeLeakDetection()`
3. Use Visual Studio's Heap Profiler for targeted inspection

### ASan Not Catching Specific Leaks

Some edge cases (e.g., leaks only on shutdown) might not be caught by ASan:

1. Fall back to Valgrind on Linux
2. Use Dr. Memory on Windows
3. Use Visual Studio's Memory Usage profiler (Diagnostics Hub)

---

## Next Steps (Optional Enhancements)

1. **Enable Dr. Memory in CI** - Uncomment the `drmemory-memcheck` job when ready
2. **Add suppression files** - Create `valgrind.supp` for known third-party leaks
3. **Memory benchmarking** - Track allocation counts over time in CI
4. **Heap profiling presets** - Add specific CMake presets for profiling tools
5. **Documentation updates** - Add memory efficiency guidelines to architecture docs

---

## References

- [ASan Documentation](https://github.com/google/sanitizers/wiki/AddressSanitizer)
- [LeakSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizerLeakSanitizer)
- [Valgrind Manual](https://valgrind.org/docs/manual/)
- [MSVC CRT Debugging](https://docs.microsoft.com/en-us/cpp/c-runtime-library/debug-heap-details)
- [Dr. Memory](https://github.com/DynamoRIO/drmemory)

---

**Status**: ??Complete - All three memory leak detection strategies implemented:
1. ??ASan/LSan enabled by default in Debug builds (Linux/macOS)
2. ??Valgrind added to CI pipeline (Linux)
3. ??CRT leak detection added to Windows Debug builds
