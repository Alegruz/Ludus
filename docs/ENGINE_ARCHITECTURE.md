# Engine Architecture

## Overview

Ludus is organized as a **layered architecture** with clear separation of concerns. This document maps the high-level structure, module dependencies, and data flow.

```
┌─────────────────────────────────────────────────────┐
│                  LudusEditor.exe                    │
│            (Application / Entry Point)              │
└──────────────────────┬──────────────────────────────┘
                       │
        ┌──────────────┴───────────────┐
        │ Platform-specific Main.cpp   │
        │ - Windows/Main.cpp           │
        │ - Unix/Main.cpp              │
        │ - MacOs/Main.cpp             │
        └──────────────┬────────────────┘
                       │
       ┌───────────────▼────────────────┐
       │  Ludus (Umbrella Interface)    │
       │  Links Core + Platform         │
       └───────────────┬────────────────┘
       ┌───────────────┴────────────────┐
       │                                │
   ┌───▼──────────┐           ┌────────▼─────┐
   │ LudusCore    │           │LudusPlatform │
   │ (Shared Lib) │           │ (Shared Lib) │
   └───┬──────────┘           └────────┬─────┘
       │                               │
       │ Public API                    │ Public API
       │ include/Ludus/Engine/Core/    │ include/Ludus/Engine/Platform/
       │ ├── Math                      │ ├── Platform.h (detection)
       │ ├── Container                 │ ├── Window.h (cross-platform)
       │ ├── Memory                    │ ├── Windows/Common.h
       │ ├── Assert                    │ ├── Unix/Common.h
       │ └── UnitTest                  │ └── MacOs/Common.h
       │                               │
       └───────────────┬───────────────┘
                       │
    ┌──────────────────▼────────────────┐
    │    External Dependencies          │
    │ ├── mimalloc (optional)           │
    │ ├── System APIs                   │
    │ │   (Windows.h, unistd.h, etc)   │
    │ └── C++ Standard Library          │
    └───────────────────────────────────┘
```

---

## Public API Organization

### LudusCore (`include/Ludus/Engine/Core/`)

**Purpose:** Lowest-level engine utilities and data structures, no platform dependencies.

#### Public Headers:

| Header | Module | Purpose |
|--------|--------|---------|
| `Common.h` | Utilities | Platform-independent macros, concepts, constants |
| `Memory.h` | Memory | Allocation/deallocation API (mimalloc wrapper) |
| `Assert.h` | Assertions | Debug assertions and error handling |
| `UnitTest.h` | Testing | Test registration and execution API |
| `Random.h` | Utilities | Pseudo-random number generation |
| `LeakDetection.h` | Debug | Memory leak detection scopes |
| `CommandLineManager.h` | CLI | Command-line argument parsing |
| `CommandLineOptions.h` | CLI | Command-line option definitions |
| **Container/** | | |
| `Array.h` | Data Structures | Dynamic array/vector |
| `String.h` | Data Structures | String utilities (char and wchar_t) |
| **Math/** | | |
| `Vector.h` | Math | 2D/3D vector types |
| `Rect.h` | Math | Rectangle types |
| `Bit.h` | Math | Bitwise operations |
| `Trigonometry.h` | Math | sin, cos, tan utilities |
| `Interpolation.h` | Math | Lerp, easing functions |
| `Integration.h` | Math | Numerical integration (quadrature) |

#### Public Namespaces:
- `ludus::core` — Core utilities
- `ludus::memory` — Memory allocation
- `ludus::math` — Math types and functions
- `ludus::container` — Container types
- `ludus::testing` — Unit test framework

---

### LudusPlatform (`include/Ludus/Engine/Platform/`)

**Purpose:** Platform abstraction layer for OS-specific functionality, unified cross-platform interfaces.

#### Public Headers:

| Header | Module | Purpose |
|--------|--------|---------|
| `Platform.h` | Detection | Platform type detection, macros, OS constants |
| `Window.h` | Window | Cross-platform window management interface |
| **Windows/** | | |
| `Common.h` | Platform | Windows-specific utilities and helpers |
| **Unix/** | | |
| `Common.h` | Platform | POSIX/Linux-specific utilities and helpers |
| **MacOs/** | | |
| `Common.h` | Platform | macOS-specific utilities and helpers |

#### Public Namespaces:
- `ludus::platform` — Platform detection and OS utilities
- `ludus::window` — Window management API

---

### LudusEditor (`include/Ludus/Editor/`)

**Purpose:** Application layer demonstrating engine usage. Entry points are platform-specific.

#### Entry Points:
- **Windows:** `src/Editor/Platform/Windows/Main.cpp` — Windows message pump, DLL integration
- **Unix/Linux:** `src/Editor/Platform/Unix/Main.cpp` — X11/Wayland event loop
- **macOS:** `src/Editor/Platform/MacOs/Main.cpp` — Cocoa event loop

---

## Module Dependencies

### Dependency Graph

```
LudusEditor (exe)
    ↓
    depends on: Ludus (interface lib)
    ├── LudusCore (shared lib)
    │   ├── mimalloc (optional, external)
    │   ├── C++ Standard Library
    │   └── System: <climits>, <cstring>, etc
    │
    └── LudusPlatform (shared lib)
        ├── LudusCore (shared lib)
        │   └── (same deps as above)
        ├── Platform-specific APIs
        │   ├── Windows: Windows.h, winsock.h, dxgi.h (placeholders)
        │   ├── Unix: unistd.h, sys/*, X11 (placeholders)
        │   └── macOS: Cocoa, CoreGraphics (placeholders)
        └── C++ Standard Library
```

### No Circular Dependencies
- **LudusCore** has no dependencies on LudusPlatform
- **LudusPlatform** depends on LudusCore (but not vice versa)
- **LudusEditor** depends on Ludus (the umbrella)

---

## High-Level Data Flow

### Initialization

```
1. LudusEditor.exe starts
   ↓
2. Platform-specific Main.cpp called (Windows/Unix/MacOs)
   ├─ Initialize platform subsystem
   ├─ Create window via LudusPlatform::Window
   ├─ Parse command line via LudusCore::CommandLineManager
   ├─ Initialize memory tracking (LudusCore::LeakDetection)
   └─ Setup signal handlers
   ↓
3. Enter message/event loop
   ├─ Service OS events (mouse, keyboard, resize)
   ├─ Update application state
   └─ Render (placeholder for future graphics)
   ↓
4. Cleanup on shutdown
   ├─ Destroy window
   ├─ Report memory leaks
   └─ Exit
```

### Runtime: Math Computation Example

```
Application code calls: ludus::math::Vector2 v = ludus::math::Lerp(start, end, t);
   ↓
Routed to: include/Ludus/Engine/Core/Math/Vector.h (public API)
   ├─ Inline template instantiation
   ├─ Executes floating-point math
   └─ Returns result
   ↓
No platform code involved (pure computation)
```

### Runtime: Window Event Example

```
OS sends event (e.g., mouse click)
   ↓
Platform-specific event handler (e.g., Windows WNDPROC)
   ├─ src/Engine/Platform/Windows/Common.cpp
   └─ Translates to ludus::window::Event
   ↓
Routed to: LudusEditor
   ├─ Processes event via public API (Window.h)
   └─ Updates state
   ↓
Application continues (event handled)
```

### Runtime: Memory Allocation Example

```
Application code calls: auto ptr = ludus::memory::Allocate<MyType>();
   ↓
Routed to: include/Ludus/Engine/Core/Memory.h (public API)
   ├─ If LUDUS_USE_MIMALLOC: calls mi_malloc()
   └─ Otherwise: calls system malloc()
   ↓
Optional leak tracking: tracked in LeakDetection scope
   ↓
Memory returned to caller
```

---

## Layer Separation

### Layer 1: LudusCore (Foundation)

**Characteristics:**
- ✅ No platform dependencies
- ✅ No external libraries (except mimalloc, optional)
- ✅ Pure computation and data structure utilities
- ✅ Single responsibility: math, memory, containers, assertions
- ✅ Easily testable in isolation

**What goes here:**
- Math (vectors, matrices, interpolation, integration)
- Containers (arrays, strings, pools)
- Memory allocation interface
- Unit test framework
- Assertions and debugging utilities

**What doesn't:**
- Window creation or OS events
- Network or file I/O
- Graphics or rendering
- Platform-specific code

### Layer 2: LudusPlatform (OS Integration)

**Characteristics:**
- ✅ Depends on LudusCore
- ✅ Platform-specific implementations hidden behind public interface
- ✅ Single responsibility: platform abstraction
- ✅ Unified API across Windows/Linux/macOS

**What goes here:**
- Window creation and management
- Event polling and handling
- Platform detection macros
- OS utilities (file paths, environment, etc)
- Graphics/rendering hooks (future)

**What doesn't:**
- Application logic
- Game/simulation code
- Complex business logic
- Data structures (use LudusCore for that)

### Layer 3: LudusEditor (Application)

**Characteristics:**
- ✅ Depends on Ludus (umbrella of Core + Platform)
- ✅ Platform-specific entry points (`Platform/<OS>/Main.cpp`)
- ✅ Demonstrates engine usage patterns

**What goes here:**
- Application entry point
- High-level event handling
- Scene/object management
- Rendering loop
- Editor features and UI

---

## Internal Code (Not Public API)

### What's Internal?

**Implementation files** (in `src/`) are not part of the public API:
- `src/Engine/Core/*.cpp` — Internal implementations of public headers
- `src/Engine/Platform/Windows/*.cpp` — Windows-specific implementations
- `src/Engine/Platform/Unix/*.cpp` — Unix/Linux-specific implementations
- `src/Engine/Platform/MacOs/*.cpp` — macOS-specific implementations
- `src/Editor/Platform/*/Main.cpp` — Platform entry points

### How to Identify Public vs Internal

**Public (in `include/Ludus/`)**
```
include/Ludus/Engine/Core/Vector.h
  │
  └─ Define public API
     - Classes: Vector2, Vector3
     - Functions: operator*, Dot(), Cross()
     - Namespaces: ludus::math
```

**Internal (in `src/`)**
```
src/Engine/Core/Container/String.cpp
  │
  └─ Implementation details
     - Private helper functions
     - Optimization code
     - Platform workarounds
```

### Rule of Thumb

| Location | Status | Usage |
|----------|--------|-------|
| `include/Ludus/**/*.h` | Public | ✅ Use freely in client code |
| `include/Ludus/**/*.hpp` | Public | ✅ Template implementations, use in client code |
| `src/**/*.cpp` | Internal | ❌ Never depend on; implementation may change |
| `src/**/*.h` | Internal | ❌ Use only within `src/` directory |
| `raddbg/**` | External | ⚠️ Third-party debugging support (optional) |

---

## Cross-Platform Strategy

### Compile-Time Detection

Platform-specific code is selected at compile time using preprocessor conditionals:

```cpp
// In Platform.h (public header)
#ifdef LUDUS_WINDOWS
    #include "Windows/Common.h"
#elif defined LUDUS_LINUX
    #include "Unix/Common.h"
#elif defined LUDUS_MAC
    #include "MacOs/Common.h"
#endif
```

### Runtime Detection

For cases where multiple platforms are enabled, use `ludus::platform::PlatformType`:

```cpp
// In application code
auto platform = ludus::platform::DetectPlatform();
if (platform == ludus::platform::PlatformType::WINDOWS) {
    // Windows-specific code
}
```

### Platform-Specific Entry Points

Each platform has a dedicated entry point:

| Platform | File | Responsible For |
|----------|------|-----------------|
| Windows | `src/Editor/Platform/Windows/Main.cpp` | WNDPROC, message loop, DLL runtime |
| Linux | `src/Editor/Platform/Unix/Main.cpp` | XLib event loop, POSIX APIs |
| macOS | `src/Editor/Platform/MacOs/Main.cpp` | Cocoa event loop, Metal (future) |

---

## Extension Points

### Adding a New Platform

1. **Create platform-specific headers:**
   ```
   include/Ludus/Engine/Platform/<NewPlatform>/
   └── Common.h (utilities, macros)
   ```

2. **Create platform-specific implementation:**
   ```
   src/Engine/Platform/<NewPlatform>/
   ├── Common.cpp
   └── Window.cpp (if implementing Window interface)
   ```

3. **Create entry point:**
   ```
   src/Editor/Platform/<NewPlatform>/
   └── Main.cpp
   ```

4. **Update CMake** (`CMakeLists.txt`):
   ```cmake
   set(PLATFORM_FOLDER "<NewPlatform>")
   ```

### Adding a New Core Module

1. **Create public header:**
   ```
   include/Ludus/Engine/Core/<NewModule>.h
   ```

2. **Create template/inline implementations (if needed):**
   ```
   include/Ludus/Engine/Core/<NewModule>.hpp
   ```

3. **Create implementation file:**
   ```
   src/Engine/Core/<NewModule>.cpp
   ```

4. **Link in CMakeLists.txt:**
   ```cmake
   target_sources(LudusCore PRIVATE src/Engine/Core/<NewModule>.cpp)
   ```

---

## CMake Build Targets

### Library Targets

| Target | Type | Location | Dependencies |
|--------|------|----------|--------------|
| `LudusCore` | Shared Lib | `build/lib/` | mimalloc (optional), C++ stdlib |
| `LudusPlatform` | Shared Lib | `build/lib/` | LudusCore, platform APIs |
| `Ludus` | Interface Lib | (header-only) | LudusCore, LudusPlatform |

### Executable Targets

| Target | Type | Location | Dependencies |
|--------|------|----------|--------------|
| `LudusEditor` | Executable | `build/bin/` | Ludus |
| `LudusTests` | Executable | `build/bin/` | LudusCore |

### Build System Features

- **Out-of-source builds:** All artifacts in `build/`, source stays clean
- **Presets:** CMakePresets.json for MSVC, Clang, GCC with Debug/Release variants
- **Static analysis:** clang-tidy, cppcheck, MSVC /analyze (optional)
- **Sanitizers:** AddressSanitizer, UndefinedBehaviorSanitizer, LeakSanitizer (Clang/GCC only)
- **Code formatting:** clang-format with format-check and format-fix targets

---

## Dependency Management

### External Dependencies

| Library | Purpose | Optional | Default |
|---------|---------|----------|---------|
| **mimalloc** | Memory allocator | Yes | ON |
| **C++23 stdlib** | Standard library | No | Always |

### Build-Time Tools (Auto-Install)

| Tool | Purpose | Default |
|------|---------|---------|
| **clang-format** | Code formatting | Auto-install if missing |
| **clang-tidy** | Static analysis | Auto-install if missing |
| **cppcheck** | Additional analysis | Manual install (OFF by default) |
| **CMake** | Build system | 3.26+ required |
| **Ninja** | Build generator | Recommended |

### No External Runtime Dependencies

After build, binaries have minimal runtime dependencies:
- ✅ LudusCore.dll/so/dylib (built)
- ✅ LudusPlatform.dll/so/dylib (built)
- ✅ LudusEditor.exe/bin (built)
- ✅ mimalloc.dll/so/dylib (optional, built)
- ✅ C++ runtime (vcruntime, libc++, etc—platform-specific)

---

## Next Steps

- **Understand Core:** See [MEMORY_ARCHITECTURE.md](MEMORY_ARCHITECTURE.md) for details on allocation strategy
- **Understand Platform:** See [GETTING_STARTED.md](GETTING_STARTED.md) for cross-platform setup
- **Write Tests:** See [UNIT_TESTING.md](UNIT_TESTING.md) for test patterns
- **Debug:** See [LEAK_DETECTION.md](LEAK_DETECTION.md) for memory profiling

