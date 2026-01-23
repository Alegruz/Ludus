# Ludus Game Engine

## Mission

**Ludus** is a modern C++23 game and visualization engine designed for educational and research purposes. It provides a **lightweight, well-architected foundation** with a focus on clarity, safety, and practical performance—not a production game engine.

## Engine Scope

Ludus includes:

- **Core Library (`LudusCore`)**: Utilities, math, containers, assertions, command-line helpers, and memory allocation strategies.
- **Platform Abstraction (`LudusPlatform`)**: Cross-platform support with unified interfaces and platform-specific implementations.
- **Editor App (`LudusEditor`)**: A sample desktop application showcasing the engine's capabilities.
- **Test Suite (`LudusTests`)**: A minimal, batteries-included unit test framework integrated into the build.

## Supported Platforms

- **Windows**: MSVC (Visual Studio 2022), Clang
- **Linux**: GCC, Clang
- **macOS**: Clang/LLVM

All platforms use a unified CMake build system with preset configurations for convenience.

---

## Quick Start

### Prerequisites

- **Windows**: Visual Studio 2022 (Desktop C++), CMake 3.26+, Git
- **Linux**: GCC or Clang, CMake 3.26+, Git
- **macOS**: Xcode Command Line Tools, CMake 3.26+, Git

Ninja is optional but recommended for faster builds. LLVM tools (clang-format, clang-tidy) are auto-installed by the build system if missing.

### Build & Run (Windows MSVC)

```powershell
# Configure and build
cmake --preset ninja_msvc-debug
cmake --build --preset ninja_msvc-debug -t LudusEditor

# Run the editor
./build/bin/LudusEditor.exe --quick-exit
```

### Build & Run Tests

```powershell
# Build tests
cmake --build --preset ninja_msvc-debug -t LudusTests

# Run tests
./build/bin/LudusTests.exe
```

### Other Platforms

- **Clang**: Use `ninja_clang-debug`, `ninja_clang-release`, or `ninja_clang-relwithdebinfo`
- **GCC (Linux)**: Use `ninja_gcc-debug`, `ninja_gcc-release`, or `ninja_gcc-relwithdebinfo`

See [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) for detailed step-by-step setup and troubleshooting.

---

## Project Architecture

### Directory Structure

```
include/Ludus/
├── Engine/
│   ├── Core/          # Utilities, math, containers, assertions
│   └── Platform/      # Platform abstraction layer
└── Editor/            # Editor app headers

src/
├── Engine/
│   ├── Core/          # LudusCore implementation
│   ├── Platform/      # LudusPlatform + platform-specific code
│   │   ├── Windows/
│   │   ├── Unix/
│   │   └── MacOs/
│   └── Renderer/      # Renderer module (placeholder)
├── Editor/
│   └── Platform/      # Platform entry points (Windows/Unix/MacOs)
└── Tests/             # LudusTests implementation

build/
├── bin/               # Executables and DLLs
└── lib/               # Libraries
```

### Component Map

```
LudusCore (shared library)
    ↓
    Core utilities, math, containers, memory
    
LudusPlatform (shared library)
    ↓
    Platform abstraction, window management, system calls
    
Ludus (interface library)
    ↓
    Umbrella target linking LudusCore + LudusPlatform
    
LudusEditor (executable)
    ↓
    Application using Ludus + platform-specific entry point
    
LudusTests (executable)
    ↓
    Test runner using in-house test registry
```

---

## Build Options

Enable or disable features via CMake options:

```powershell
cmake --preset ninja_msvc-debug -DENABLE_MIMALLOC=OFF -DENABLE_SANITIZERS=ON
```

- `ENABLE_MIMALLOC=ON|OFF` (default ON): High-performance memory allocator. Auto-disabled with sanitizers.
- `ENABLE_SANITIZERS=ON|OFF` (default ON for Clang/GCC): AddressSanitizer, UndefinedBehaviorSanitizer, LeakSanitizer.
- `ENABLE_CLANG_TIDY=ON|OFF` (default ON): Static analysis with clang-tidy.
- `ENABLE_CPPCHECK=ON|OFF`, `ENABLE_MSVC_ANALYZE=ON|OFF`: Optional additional analyzers.

---

## Documentation

New to Ludus? **Start here:** [docs/README.md](docs/README.md)

It provides a navigation guide to all documentation. For architecture details, see [docs/ENGINE_ARCHITECTURE.md](docs/ENGINE_ARCHITECTURE.md).

Quick reference to key docs:

| Topic | Document |
|-------|----------|
| **🏗️ Engine Architecture & Public API** | [ENGINE_ARCHITECTURE.md](docs/ENGINE_ARCHITECTURE.md) |
| **Setup & First Build** | [GETTING_STARTED.md](docs/GETTING_STARTED.md) |
| **Build System & CI** | [BUILD_SYSTEM.md](docs/BUILD_SYSTEM.md) |
| **Unit Testing** | [UNIT_TESTING.md](docs/UNIT_TESTING.md) |
| **Memory Architecture** | [MEMORY_ARCHITECTURE.md](docs/MEMORY_ARCHITECTURE.md) |
| **Leak Detection** | [LEAK_DETECTION.md](docs/LEAK_DETECTION.md) |
| **Mimalloc Integration** | [MIMALLOC_INTEGRATION.md](docs/MIMALLOC_INTEGRATION.md) |
| **Static Analysis** | [STATIC_ANALYSIS.md](docs/STATIC_ANALYSIS.md) |

---

## Common Tasks

### Add a Unit Test

1. Write test in `src/Engine/Core/CoreTests.cpp` or create a new file in `src/Engine/Core/`
2. Register via `LUDUS_TEST` macro
3. Run: `./build/bin/LudusTests.exe`

See [UNIT_TESTING.md](docs/UNIT_TESTING.md) for details.

### Add a New Platform

1. Create `src/Engine/Platform/<YourPlatform>/Common.cpp`
2. Add corresponding headers under `include/Ludus/Engine/Platform/<YourPlatform>/`
3. Update CMake's `PLATFORM_FOLDER` mapping
4. Implement platform-specific entry point in `src/Editor/Platform/<YourPlatform>/Main.cpp`

### Debug Memory Leaks

Use the built-in leak detector:

```cpp
LUDUS_LEAK_DETECTOR(ScopeName) {
    // Code to profile
}
```

See [LEAK_DETECTION.md](docs/LEAK_DETECTION.md) and [LEAK_DETECTION_QUICK_REFERENCE.md](docs/LEAK_DETECTION_QUICK_REFERENCE.md).

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| **LLVM tools not found** | CMake attempts auto-install via Chocolatey/Homebrew/apt. See [BUILD_SYSTEM.md](docs/BUILD_SYSTEM.md) for manual install steps. |
| **Missing Ninja** | Install: `choco install ninja` (Windows), `brew install ninja` (macOS), `apt install ninja-build` (Linux). |
| **Build artifacts in source root** | Delete old `.exe`, `.dll`, `.ilk` files. New builds output to `build/bin/` and `build/lib/`. |
| **ASan on Windows** | MSVC ASan is disabled; use Clang presets (`ninja_clang-*`) instead. |

---

## License

See [LICENSE](LICENSE).
