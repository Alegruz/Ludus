# Memory Allocation Architecture

## Overview

The Ludus engine uses a centralized memory allocation strategy to ensure:

1. **Type safety** - Safe allocation/deallocation for all types
2. **Performance** - High-performance mimalloc as the base allocator
3. **Debuggability** - Centralized tracking and profiling
4. **Flexibility** - Easy to swap allocators or add custom pools

---

## Base Allocator: mimalloc

### Why mimalloc?

**mimalloc** (Microsoft) is the chosen base allocator for Ludus because:

| Criterion | mimalloc | Advantages |
|-----------|----------|------------|
| **Performance** | Excellent | Optimized for games, low latency |
| **Fragmentation** | Very low | Advanced binning and free-space merging |
| **Threading** | Exceptional | Per-thread heaps, lock-free design |
| **Multiplatform** | Yes | Windows, Linux, macOS, BSD |
| **Integration** | Simple | Single library, CMake FetchContent |
| **Statistics** | Built-in | Memory tracking without overhead |
| **Override** | Automatic | `MI_OVERRIDE` replaces malloc/free globally |

### Comparison with Alternatives

| Allocator | Games | Linux/Mac | Stats | Override | Complexity |
|-----------|-------|----------|-------|----------|-----------|
| **mimalloc** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ✅ | Low |
| **jemalloc** | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⚠️ | Medium |
| **tcmalloc** | ⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ | ⚠️ | High |
| **System malloc** | ⭐⭐ | ⭐⭐ | ⚠️ | N/A | N/A |

---

## Integration Architecture

### 1. CMake FetchContent (Automatic Download)

```cmake
# CMakeLists.txt - mimalloc is automatically fetched and built
FetchContent_Declare(mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)

set(MI_OVERRIDE ON)  # Automatically override malloc/free
FetchContent_MakeAvailable(mimalloc)
```

### 2. Engine Linking

mimalloc is linked to `LudusBuildSettings`, which is inherited by all engine targets:

```
LudusBuildSettings (INTERFACE)
    ├── ProjectSanitizersSettings
    └── mimalloc (MI_OVERRIDE=ON)

Targets using LudusBuildSettings:
    ├── LudusCore (shared lib)
    ├── LudusPlatform (shared lib)
    └── LudusEditor (executable)
```

**Effect**: All malloc/free calls throughout the engine automatically use mimalloc.

### 3. Memory Allocation API

Rather than direct `malloc`/`free`, the engine uses typed allocation functions:

```cpp
#include <Ludus/Engine/Core/Memory.h>

using namespace ludus::memory;

// Allocate
int* array = Allocate<int>(100);

// Reallocate
int* newArray = Reallocate(array, 200);

// Deallocate
Deallocate(newArray);

// Memory operations
CopyMemory(dest, src, size);
MoveMemory(dest, src, size);
ZeroOutMemory(sensitive, size);
```

**Benefits**:
- Type-safe (no casting required)
- Easy to intercept for debugging
- Consistent across the codebase
- Replaceable with custom allocators

---

## Current Issues (Why This Matters)

### Problem: Raw malloc/free in Containers

The current [Array](../include/Ludus/Engine/Core/Container/Array.hpp) implementation uses raw `malloc`/`free`:

```cpp
// ❌ Current (problematic)
T* newData = static_cast<T*>(malloc(capacity * sizeof(T)));
free(mData);
memcpy(mData, src, size);
```

**Issues**:
1. **UB for non-trivial types** - `memcpy` is undefined behavior for types with custom move/copy semantics
2. **No constructor/destructor calls** - Skips RAII semantics
3. **Manual memory management** - Easy to leak or double-free
4. **Inconsistent** - Mixes malloc/free with operator new/delete patterns
5. **No allocation tracking** - Can't profile memory usage per container

### Solution: Migrate to Memory API

```cpp
// ✅ Improved (type-safe)
T* newData = Allocate<T>(capacity);
Deallocate(mData);
CopyMemory(mData, src, size);  // Still OK for trivially copyable types
```

---

## Migration Path

### Phase 1: API Introduction (✅ Complete)
- [x] Create `ludus::memory` namespace with allocation functions
- [x] Document API in [Memory.h](../include/Ludus/Engine/Core/Memory.h)

### Phase 2: Container Updates (✅ Complete)
- [x] Update [Array](../include/Ludus/Engine/Core/Container/Array.hpp) to use Memory API
- [ ] Update [String](../include/Ludus/Engine/Core/Container/String.hpp) for UTF conversions (next)
- [ ] Add constructor/destructor support for non-trivial types (future)

### Phase 3: Subsystem Integration (🔄 Pending)
- [ ] Update `CommandLineManager` allocations (depends on Array updates)
- [ ] Add platform-specific allocations (Windows/Linux/macOS)
- [ ] Implement custom heaps for hot-path subsystems

### Phase 4: Profiling & Optimization (🔄 Pending)
- [ ] Add memory statistics gathering
- [ ] Profile real workloads
- [ ] Identify and optimize hot allocations
- [ ] Consider specialized pools for frequently-sized objects

---

## Configuration Options

### Enable/Disable mimalloc

```bash
# Enable (default)
cmake -DENABLE_MIMALLOC=ON

# Disable (use system malloc)
cmake -DENABLE_MIMALLOC=OFF
```

### Pin to Stable Version

Edit [CMakeLists.txt](../CMakeLists.txt):

```cmake
FetchContent_Declare(mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG v2.1.2  # Pin to stable release
)
```

---

## Memory Profiling

### Runtime Statistics

```cpp
#include <mimalloc.h>

// Print heap stats
mi_stats_print(NULL);

// Get current heap usage
mi_heap_t* heap = mi_heap_get_default();
size_t used = mi_heap_used(heap);
size_t committed = mi_heap_committed(heap);
```

### Environment Variables

```bash
export MIMALLOC_VERBOSE=1          # Print allocation activity
export MIMALLOC_SHOW_STATS=1       # Show stats on exit
export MIMALLOC_PAGE_RESET=0       # Keep freed pages (for Valgrind)
```

### Profiling Tools

```bash
# Linux: perf (requires frame pointers)
perf record -F 99 -g ./LudusEditor
perf report

# Linux: Valgrind
valgrind --tool=massif ./LudusEditor
ms_print massif.out.*

# macOS: Instruments (built-in)
xcrun xctrace record --template "System Trace" ./LudusEditor
```

---

## Container Code: Critical Areas

### Array (✅ Migrated)

Current: [Array.hpp](../include/Ludus/Engine/Core/Container/Array.hpp)

```cpp
// ✅ Now using Memory API
T* newData = ludus::memory::Allocate<T>(capacity);
ludus::memory::CopyMemory(newData, mData, mSize * sizeof(T));
ludus::memory::Deallocate(mData);
```

**Status**: All malloc/free/memcpy calls replaced with type-safe Memory API.

### String (Needs Update)

Current: [String.cpp](../src/Engine/Core/Container/String.cpp)

```cpp
// Line 47: New allocation for UTF conversion
wchar_t* buffer = new wchar_t[requiredSize];

// Should be:
wchar_t* buffer = ludus::memory::Allocate<wchar_t>(requiredSize);
```

**Impact**: All string conversions (UTF-8 ↔ UTF-16 on Windows).

### CommandLineManager (Needs Update)

Current: [CommandLineManager.hpp](../include/Ludus/Engine/Core/CommandLineManager.hpp)

Uses `DynamicArray<BasicString<CharT>>` which will inherit Memory API usage once Array is updated.

---

## Future Enhancements

### 1. Custom Heaps per Subsystem

```cpp
namespace ludus::memory
{
    // Create dedicated heaps for different subsystems
    mi_heap_t* renderHeap = mi_heap_new();
    mi_heap_t* audioHeap = mi_heap_new();
    
    // Allocate from specific heap
    Buffer* buf = mi_heap_malloc(renderHeap, size);
}
```

### 2. Memory Pools

```cpp
namespace ludus::memory
{
    template<typename T, size_t PoolSize>
    class ObjectPool
    {
        // Pre-allocated pool of T objects
        // Reuses memory for frequently-allocated types
    };
}
```

### 3. Allocation Tracking

```cpp
namespace ludus::memory
{
    struct AllocationInfo
    {
        const char* subsystem;
        size_t size;
        const char* file;
        int line;
    };
    
    // Hook into mimalloc tracking
    void InstallTrackingHook(AllocationTracker* tracker);
}
```

### 4. Memory Budgets

```cpp
namespace ludus::memory
{
    class MemoryBudget
    {
        size_t maxSize;
        size_t currentUsage;
        bool CheckAllocation(size_t size);
    };
}
```

---

## References

- [mimalloc GitHub](https://github.com/microsoft/mimalloc)
- [mimalloc Documentation](https://microsoft.github.io/mimalloc/)
- [Game Engine Memory](https://www.gamedev.net/tutorials/general/programming-techniques/understanding-memory-allocation/)
- [Modern C++ Memory](https://www.stroustrup.com/glossary.html#memory-management)

---

## Checklist

- [x] Choose mimalloc as base allocator
- [x] Integrate via CMake FetchContent
- [x] Create `ludus::memory` API
- [x] Document architecture
- [ ] Update Array to use Memory API
- [ ] Update String to use Memory API
- [ ] Add memory profiling to CI
- [ ] Implement custom heaps for hot paths
- [ ] Add allocation budgets
