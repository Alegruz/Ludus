# Ludus Documentation Hub

Welcome to the Ludus documentation. This guide maps out all documentation, explains the learning path for contributors, and helps you find what you need.

---

## Learning Path: Core → Platform → Editor

The Ludus engine is organized in layers. Understand them in this order:

```
┌──────────────────────────────────────────────┐
│         LudusEditor (Application)            │
│  (Runs on top of Ludus via entry points)     │
└────────────────────┬─────────────────────────┘
                     │
┌────────────────────▼─────────────────────────┐
│    Ludus (Interface/Umbrella Library)        │
│  (Links Core + Platform together)            │
└────────────────────┬─────────────────────────┘
       ┌────────────┴────────────┐
       │                         │
┌──────▼──────────┐   ┌─────────▼────────┐
│   LudusCore     │   │  LudusPlatform   │
│                 │   │                  │
│ - Math          │   │ - Window/Events  │
│ - Containers    │   │ - System calls   │
│ - Utilities     │   │ - Platform glue  │
│ - Assertions    │   │ - Multi-platform │
│ - Memory        │   │   (Win/Lin/Mac)  │
└─────────────────┘   └──────────────────┘
```

**Start here if you're new to the codebase:**

1. **[GETTING_STARTED.md](GETTING_STARTED.md)** — Setup, build, run for the first time
2. **[Understand Core →](GETTING_STARTED.md#where-to-read-next)** [MEMORY_ARCHITECTURE.md](MEMORY_ARCHITECTURE.md) — How memory allocation works
3. **Understand Platform** — How to add support for new platforms or extend window/OS integration
4. **Understand Editor** — How to extend the sample application

---

## Complete Documentation Map

### 🚀 Getting Started (Start Here!)

| Document | Purpose | Audience |
|----------|---------|----------|
| **[GETTING_STARTED.md](GETTING_STARTED.md)** | Step-by-step setup, first build, running editor and tests | Everyone, especially newcomers |

### 🏗️ Architecture & Design

| Document | Purpose | Audience |
|----------|---------|----------|
| **[MEMORY_ARCHITECTURE.md](MEMORY_ARCHITECTURE.md)** | Memory allocation strategy, mimalloc integration, custom pools | Engine developers, performance engineers |
| **[BUILD_SYSTEM.md](BUILD_SYSTEM.md)** | CMake philosophy, presets, auto-install behavior, CI integration | DevOps, maintainers, CI/CD engineers |

### 🧪 Testing & Quality

| Document | Purpose | Audience |
|----------|---------|----------|
| **[UNIT_TESTING.md](UNIT_TESTING.md)** | Writing and running unit tests, test registry API | Developers adding/maintaining tests |
| **[STATIC_ANALYSIS.md](STATIC_ANALYSIS.md)** | clang-tidy, cppcheck, MSVC analysis setup and integration | Code quality engineers |

### 🔍 Debugging & Profiling

| Document | Purpose | Audience |
|----------|---------|----------|
| **[LEAK_DETECTION.md](LEAK_DETECTION.md)** | Memory leak detection with sanitizers and custom detectors | Debug-focused developers |
| **[LEAK_DETECTION_QUICK_REFERENCE.md](LEAK_DETECTION_QUICK_REFERENCE.md)** | Quick syntax reference for leak detection macros | Developers using leak detector |
| **[MIMALLOC_INTEGRATION.md](MIMALLOC_INTEGRATION.md)** | Mimalloc setup, statistics gathering, profiling | Performance engineers |

---

## Quick Navigation by Task

**I want to...**

- **Get the engine running for the first time**  
  → [GETTING_STARTED.md](GETTING_STARTED.md)

- **Write a unit test**  
  → [UNIT_TESTING.md](UNIT_TESTING.md)

- **Debug a memory leak**  
  → [LEAK_DETECTION.md](LEAK_DETECTION.md) or [LEAK_DETECTION_QUICK_REFERENCE.md](LEAK_DETECTION_QUICK_REFERENCE.md)

- **Understand how memory is allocated**  
  → [MEMORY_ARCHITECTURE.md](MEMORY_ARCHITECTURE.md)

- **Set up CI/CD or modify the build system**  
  → [BUILD_SYSTEM.md](BUILD_SYSTEM.md)

- **Enable static analysis (clang-tidy, cppcheck)**  
  → [STATIC_ANALYSIS.md](STATIC_ANALYSIS.md)

- **Profile memory with mimalloc**  
  → [MIMALLOC_INTEGRATION.md](MIMALLOC_INTEGRATION.md)

---

## Architecture Overview

### LudusCore

**Headers:** `include/Ludus/Engine/Core/`  
**Implementation:** `src/Engine/Core/`  
**Output:** `build/lib/LudusCore.dll` (Windows) or `.so`/`.dylib` (Unix/macOS)

Contains the **engine's foundation**:
- Math utilities (`Vector`, `Rect`, `Interpolation`, `Trigonometry`, `Bit` operations)
- Container types (`Array`, `String`)
- Memory management and tracking
- Assertions and error handling
- Command-line parsing
- Minimal unit test framework

**Key Files:**
- `Memory.h` — Allocation/deallocation API
- `Random.h` — Random number generation
- `Assert.h` — Assertion macros
- `UnitTest.h` — Test registration API

**When to use:** Implement low-level engine systems, math-heavy code, or utilities that don't touch OS APIs.

### LudusPlatform

**Headers:** `include/Ludus/Engine/Platform/`  
**Implementation:** `src/Engine/Platform/`  
**Platform-specific code:** `src/Engine/Platform/{Windows,Unix,MacOs}/`  
**Output:** `build/lib/LudusPlatform.dll` (Windows) or `.so`/`.dylib` (Unix/macOS)

Provides **platform abstraction** and OS integration:
- Window management (`Window` class)
- Event handling
- Platform detection and constants
- System-level glue code

**Key Files:**
- `Platform.h` — Platform detection, OS constants
- `Window.h` — Cross-platform window interface
- `Windows/Common.h`, `Unix/Common.h` — Platform-specific helpers

**When to use:** Need to create windows, interact with OS events, or add new platform support.

### Ludus (Umbrella)

**Purpose:** Interface library that combines `LudusCore` + `LudusPlatform`

Targets that depend on `Ludus` automatically get both libraries without managing two dependencies. This is the recommended target for applications.

### LudusEditor

**Entry points:**  
- Windows: `src/Editor/Platform/Windows/Main.cpp`
- Unix/Linux: `src/Editor/Platform/Unix/Main.cpp`
- macOS: `src/Editor/Platform/MacOs/Main.cpp`

**Output:** `build/bin/LudusEditor.exe` (Windows) or `LudusEditor` (Unix/macOS)

A sample **desktop application** demonstrating:
- Platform entry point pattern
- Window creation and event handling
- Memory leak detection setup
- Integration with LudusCore and LudusPlatform

**When to modify:** Extending the sample app, adding UI frameworks, or testing platform-specific features.

### LudusTests

**Entry point:** `src/Tests/Main.cpp`  
**Test files:** `src/Engine/Core/CoreTests.cpp` and others  
**Output:** `build/bin/LudusTests.exe` (Windows) or `LudusTests` (Unix/macOS)

Minimal **test runner** using the in-house `LUDUS_TEST` registry.

**When to use:** Add unit tests for Core systems, math utilities, containers, or other low-level components.

---

## Common Workflows

### Adding a Unit Test

1. **Write your test:**
   ```cpp
   // In src/Engine/Core/CoreTests.cpp or a new file
   #include "Ludus/Engine/Core/UnitTest.h"
   
   LUDUS_TEST(MyMathTest) {
       LUDUS_ASSERT(2 + 2 == 4);
       return true;
   }
   ```

2. **Build and run:**
   ```powershell
   cmake --build --preset ninja_msvc-debug -t LudusTests
   ./build/bin/LudusTests.exe
   ```

See [UNIT_TESTING.md](UNIT_TESTING.md) for details.

### Adding a New Platform

1. **Create platform-specific code:**
   ```
   src/Engine/Platform/<NewPlatform>/
   ├── Common.cpp
   └── include/Ludus/Engine/Platform/<NewPlatform>/
       └── Common.h
   ```

2. **Update CMake** to detect and include your platform.

3. **Create an entry point:**
   ```
   src/Editor/Platform/<NewPlatform>/Main.cpp
   ```

4. **Link and rebuild.**

### Using Memory Leak Detection

**Quick example:**
```cpp
#include "Ludus/Engine/Core/LeakDetection.h"

int main() {
    LUDUS_LEAK_DETECTOR(MainScope) {
        // Your code here
        // Leaks reported on scope exit
    }
    return 0;
}
```

See [LEAK_DETECTION_QUICK_REFERENCE.md](LEAK_DETECTION_QUICK_REFERENCE.md) for more.

---

## Contributing & Code Organization

### Directory Conventions

- **`include/Ludus/`** — All public headers, mirroring the namespace and module structure
- **`src/`** — Implementation files, organized by module
- **`build/`** — Build output (generated, not in version control)
- **`cmake/`** — Helper CMake scripts
- **`tools/`** — Scripts and utilities (install-llvm.ps1, etc.)
- **`docs/`** — This documentation

### Header Organization

- Public headers in `include/Ludus/Engine/Core/` use `.h` extension for interfaces
- Implementation headers in `include/Ludus/Engine/Core/` use `.hpp` for templates and inline functions
- Example: `Array.h` (interface) + `Array.hpp` (template implementation)

### Testing

- Test files live alongside their implementation in `src/Engine/Core/`
- Register tests using the `LUDUS_TEST` macro
- Run via `LudusTests` executable

### Code Quality

- **clang-format** (auto-installed) enforces style
- **clang-tidy** (auto-installed) checks best practices and warnings-as-errors
- **cppcheck** (optional) for additional static analysis
- **Sanitizers** (ASan, UBSan, LSan) enabled by default on Clang/GCC

Enable all checks:
```powershell
cmake --preset ninja_clang-debug -DENABLE_SANITIZERS=ON -DENABLE_CLANG_TIDY=ON -DENABLE_CPPCHECK=ON
```

---

## Troubleshooting

**Still stuck?** Check the relevant document:

| Problem | Document |
|---------|----------|
| Build fails | [BUILD_SYSTEM.md](BUILD_SYSTEM.md) |
| Tests won't run | [UNIT_TESTING.md](UNIT_TESTING.md) |
| Memory leak suspected | [LEAK_DETECTION.md](LEAK_DETECTION.md) |
| Clang-tidy/format issues | [STATIC_ANALYSIS.md](STATIC_ANALYSIS.md) |
| Performance concerns | [MEMORY_ARCHITECTURE.md](MEMORY_ARCHITECTURE.md) or [MIMALLOC_INTEGRATION.md](MIMALLOC_INTEGRATION.md) |

---

## Document Maintenance

This hub is your one-stop reference. When adding new documentation:

1. Add an entry to this file in the appropriate section
2. Include a one-line purpose and intended audience
3. Ensure cross-links are updated
4. Keep sections organized by theme (Getting Started → Architecture → Testing → Debugging)

---

**Ready to start?** Begin with [GETTING_STARTED.md](GETTING_STARTED.md).
