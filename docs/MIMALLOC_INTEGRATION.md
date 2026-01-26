# mimalloc Integration

Status: draft
Owner: maintainers
Last updated: 2026-01-26


## Overview

[mimalloc](https://github.com/microsoft/mimalloc) is a high-performance memory allocator from Microsoft and the **chosen base allocator** for the Ludus engine.

### Why mimalloc?

**mimalloc is selected for Ludus because:**

- **Game-optimized** - Excellent latency and throughput for typical game workloads
- **Low fragmentation** - Advanced allocation strategy minimizes wasted memory
- **Thread-friendly** - Per-thread heaps with lock-free design for multicore games
- **Multiplatform** - Seamless support for Windows, Linux, macOS, BSD
- **Drop-in replacement** - Overrides malloc/free globally with `MI_OVERRIDE=ON`
- **Built-in profiling** - Memory statistics without external tools
- **Easy integration** - CMake FetchContent + single library

See [MEMORY_ARCHITECTURE.md](MEMORY_ARCHITECTURE.md) for detailed architecture and comparison with alternatives.

## Integration

### Building with mimalloc

By default, mimalloc is **enabled** in all builds:

```bash
cmake --preset ninja_gcc-debug
cmake --build --preset ninja_gcc-debug
# mimalloc is automatically linked and overrides malloc/free
```

### Disabling mimalloc

If you need to use the system allocator instead:

```bash
cmake -DENABLE_MIMALLOC=OFF --preset ninja_gcc-debug
cmake --build --preset ninja_gcc-debug
```

### Presets with mimalloc

All standard presets include mimalloc by default:
- `ninja_clang-debug`
- `ninja_clang-release`
- `ninja_clang-relwithdebinfo`
- `ninja_gcc-debug`
- `ninja_gcc-release`
- `ninja_gcc-relwithdebinfo`
- `ninja_msvc-debug`
- `ninja_msvc-release`
- `ninja_msvc-relwithdebinfo`

## Building from Source

mimalloc is fetched automatically using CMake's `FetchContent` during configuration. The first build will:

1. Clone mimalloc from GitHub (main branch)
2. Configure it as a static library
3. Link it into `LudusBuildSettings` (inherited by all targets)

**Note**: To pin to a stable version, edit [CMakeLists.txt](CMakeLists.txt) and change:
```cmake
GIT_TAG main  # Change to GIT_TAG v2.1.2 (or desired version)
```

## Using mimalloc Features

### Runtime Statistics

Once mimalloc is linked, you can query memory statistics at runtime:

```cpp
#include <mimalloc.h>

// Print current stats
mi_stats_print(NULL);

// Get heap info
mi_heap_t* heap = mi_heap_get_default();
size_t used = mi_heap_used(heap);
size_t committed = mi_heap_committed(heap);
```

### Custom Allocators

For sensitive allocations, you can use mimalloc's custom heap API:

```cpp
#include <mimalloc.h>

// Create a dedicated heap
mi_heap_t* custom_heap = mi_heap_new();

// Allocate from it
void* ptr = mi_heap_malloc(custom_heap, size);

// Delete when done
mi_heap_delete(custom_heap);
```

## Memory Profiling with mimalloc

### Enable Detailed Logging

Set environment variable before running:

```bash
# Linux/macOS
export MIMALLOC_VERBOSE=1
./LudusEditor

# Windows
set MIMALLOC_VERBOSE=1
LudusEditor.exe
```

Available options:
- `MIMALLOC_VERBOSE=1` - Print statistics and activity
- `MIMALLOC_SHOW_STATS=1` - Show detailed stats on exit
- `MIMALLOC_PAGE_RESET=0` - Keep freed pages committed (for leak detection)

### Memory Tracking

To track allocations more carefully:

```bash
# Linux/macOS - Use with perf/valgrind
valgrind --tool=massif ./LudusEditor
# Then view with: ms_print massif.out.<pid>

# Or use perf with frame pointers
perf record -F 99 -g ./LudusEditor
perf report
```

## Performance Comparison

To compare allocator performance:

**Baseline (system malloc)**:
```bash
cmake -DENABLE_MIMALLOC=OFF -B out/build/baseline
cmake --build --preset ninja_gcc-debug -B out/build/baseline
time ./out/build/baseline/src/Editor/LudusEditor --bench
```

**With mimalloc**:
```bash
cmake -DENABLE_MIMALLOC=ON -B out/build/mimalloc
cmake --build --preset ninja_gcc-debug -B out/build/mimalloc
time ./out/build/mimalloc/src/Editor/LudusEditor --bench
```

Compare timing and memory usage.

## Integration with Container Code

mimalloc is particularly useful for profiling the custom container classes:

- [Array.hpp](../include/Ludus/Engine/Core/Container/Array.hpp) - Dynamic resizing
- [String.cpp](../src/Engine/Core/Container/String.cpp) - UTF conversions
- [CommandLineManager](../include/Ludus/Engine/Core/CommandLineManager.h)

mimalloc tracks all allocations from these containers automatically.

## CI/CD Integration

mimalloc is included in all CI builds by default. To track allocator statistics in CI:

1. Run with `MIMALLOC_SHOW_STATS=1` to dump stats at program exit
2. Upload stats as artifacts for comparison across builds
3. Track allocation patterns over time

Example CI step:
```yaml
- name: Run with mimalloc stats
  env:
    MIMALLOC_SHOW_STATS: 1
  run: ./LudusEditor 2>&1 | tee mimalloc-stats.txt

- name: Upload stats
  uses: actions/upload-artifact@v4
  with:
    name: mimalloc-stats
    path: mimalloc-stats.txt
```

## Troubleshooting

### mimalloc Not Overriding malloc

Ensure `MI_OVERRIDE=ON` is set:
```cmake
set(MI_OVERRIDE ON CACHE BOOL "Override malloc/free with mimalloc" FORCE)
```

This is already configured in [CMakeLists.txt](CMakeLists.txt).

### Build Fails: Git Not Found

If `FetchContent` fails to clone mimalloc, ensure Git is installed:

```bash
# Linux
sudo apt-get install git

# macOS
brew install git

# Windows
# Download from https://git-scm.com/download/win
```

### Slow Build First Time

First configure with mimalloc will download and build it (~1-2 minutes). Subsequent builds use cache.

To skip FetchContent rebuild:
```bash
cmake --build --preset ninja_gcc-debug --target LudusEditor  # Rebuild only the target
```

## References

- [mimalloc GitHub](https://github.com/microsoft/mimalloc)
- [mimalloc Documentation](https://microsoft.github.io/mimalloc/)
- [CMake FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html)

## Future Enhancements

- [ ] Add mimalloc memory statistics to debug overlay
- [ ] Track allocation patterns per subsystem
- [ ] Add custom heap for per-allocator profiling
- [ ] Compare with other allocators (jemalloc, tcmalloc)
