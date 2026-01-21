# Memory Leak Detection Quick Reference

## What Changed?

Three key improvements to catch memory leaks in the Ludus container code:

### 1. **Linux/macOS: ASan + LSan (Default)**
- ✅ Enabled automatically in Debug builds
- ✅ Also works in RelWithDebInfo optimized builds  
- Catches: buffer overflows, use-after-free, leaks

### 2. **Linux: Valgrind (CI)**
- ✅ Runs automatically on GitHub Actions
- Catches: leaks ASan might miss, deep validation
- Reports uploaded as artifacts

### 3. **Windows: CRT Leak Detection (Default)**
- ✅ Enabled automatically in Debug builds
- Leaks reported in Visual Studio Debug Output on exit
- API available: `ludus::core::debug::*` functions

---

## Quick Start

### Windows Developer
```powershell
cmake --preset ninja_msvc-debug
cmake --build --preset ninja_msvc-debug
# Run and check Visual Studio Debug Output for leaks on exit
```

### Linux Developer
```bash
cmake --preset ninja_gcc-debug
cmake --build --preset ninja_gcc-debug
./out/build/ninja_gcc-debug/src/Editor/LudusEditor
# Sanitizer output appears in stderr automatically
```

### Test with Optimizations
```bash
cmake --preset ninja_gcc-relwithdebinfo  # New!
cmake --build --preset ninja_gcc-relwithdebinfo
# Run with optimizations + sanitizers enabled
```

---

## Files Added/Modified

### New Files
- [LeakDetection.h](../include/Ludus/Engine/Core/LeakDetection.h) - Windows CRT helpers
- [LEAK_DETECTION.md](../docs/LEAK_DETECTION.md) - Detailed guide
- [MEMORY_LEAK_DETECTION_IMPLEMENTATION.md](../docs/MEMORY_LEAK_DETECTION_IMPLEMENTATION.md) - Implementation details

### Modified Files
- [CMakeLists.txt](../CMakeLists.txt) - Sanitizers on by default
- [CMakePresets.json](../CMakePresets.json) - Added RelWithDebInfo presets
- [Main.cpp (Windows)](../src/Editor/Platform/Windows/Main.cpp) - CRT integration
- [CI Workflow](.github/workflows/cmake-multi-platform.yml) - Valgrind job + Dr. Memory option

---

## Useful API (Windows)

```cpp
#include <Ludus/Engine/Core/LeakDetection.h>

// Initialize at startup (already done in Main.cpp)
ludus::core::debug::InitializeLeakDetection();

// In your code
ludus::core::debug::SetBreakOnAllocation(12345);  // Break on specific alloc
bool valid = ludus::core::debug::IsHeapValid();   // Check heap integrity
ludus::core::debug::DumpMemoryLeaks();             // Manual dump
```

---

## CI Coverage

| Platform | Build | Tool | Automatic |
|----------|-------|------|-----------|
| Linux | Debug | ASan/LSan + Valgrind | ✅ Yes |
| Windows | Debug | CRT Leak Check + ASan | ✅ Yes |
| macOS | Debug | ASan/LSan | ✅ Yes |

---

## Need Help?

- **General guide**: See [LEAK_DETECTION.md](../docs/LEAK_DETECTION.md)
- **Implementation details**: See [MEMORY_LEAK_DETECTION_IMPLEMENTATION.md](../docs/MEMORY_LEAK_DETECTION_IMPLEMENTATION.md)
- **Visual Studio UI**: Debug → Performance Profiler → Memory Usage
- **Dr. Memory (Windows)**: Optional - see workflow comments for enablement

---

**Status**: All three memory leak detection strategies now active! 🎉
