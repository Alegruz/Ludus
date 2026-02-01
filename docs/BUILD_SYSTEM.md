# Build System & CI Integration

Status: draft
Owner: maintainers
Last updated: 2026-01-30

This document explains the Ludus build philosophy, onboarding flow, and how to integrate into CI/CD environments.

## Philosophy: Build = Build, Onboarding = Install

1. **CMake only builds**
   - The configure step never installs system packages.
   - Missing tools are reported with guidance to run the onboarding app.

2. **Onboarding owns prerequisites**
   - `init.bat` / `init.sh` installs required tools and optional developer utilities.
   - Role-specific installs are supported (user, contributor, debugger).

3. **Stable external dependencies**
   - Dependencies like mimalloc and Vulkan headers are pinned to known versions.
   - Update via explicit changes (not auto-fetch).

## Development Setup

### Onboarding (Recommended)

Run the onboarding app to install prerequisites and optional tooling:

```powershell
./init.bat
```

```bash
./init.sh
```

This installs CMake, Git, compiler toolchains (where possible), LLVM tools, VS Code, and optional debugging tools like RadDbg on Windows.

### Manual Tool Installation

If you prefer manual control or are in a restricted environment:

**Windows:**
```powershell
# LLVM tools
powershell -ExecutionPolicy Bypass -File tools/install-llvm.ps1
# Or
choco install llvm
```

**macOS:**
```bash
brew install llvm
```

**Linux (Debian/Ubuntu):**
```bash
sudo apt-get install clang clang-tools
```

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Build & Test

on: [push, pull_request]

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3

      - name: Install LLVM (optional, for clang-tidy)
        run: choco install llvm -y

      - name: Configure
        run: cmake --preset ninja_msvc-debug

      - name: Build
        run: cmake --build --preset ninja_msvc-debug -t LudusEditor

      - name: Run Tests
        run: |
          cmake --build --preset ninja_msvc-debug -t LudusTests
          ./build/bin/LudusTests.exe

      - name: Check Formatting (optional)
        run: cmake --build --preset ninja_msvc-debug -t format-check
        continue-on-error: true
```

**Key points:**
- LLVM install is optional. If missing, clang-tidy/format targets are skipped.
- Core builds and tests still run.

### Docker/Linux CI Example

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git

RUN apt-get install -y clang clang-tools

WORKDIR /ludus
COPY . .

RUN cmake --preset ninja_clang-debug
RUN cmake --build --preset ninja_clang-debug -t LudusEditor
RUN cmake --build --preset ninja_clang-debug -t LudusTests
RUN ./build/bin/LudusTests
```

## FAQ

**Q: Does CMake install tools automatically?**  
A: No. Install prerequisites with `init.bat` / `init.sh` or manually.

**Q: What happens if clang-tidy/clang-format are missing?**  
A: Builds succeed, but formatting/static analysis targets are unavailable. CMake prints guidance.

**Q: Can I use sanitizers in CI?**  
A: Yes, on Linux with Clang/GCC. Use sanitizer-enabled presets like `ninja_clang-relwithdebinfo`.

## Stable External Dependency Versions

Keep these pinned versions updated as you test and validate new releases:

- **mimalloc**: Currently `v2.2.7`.

## Related

- See [GETTING_STARTED.md](GETTING_STARTED.md) for local setup.
- See [CMakeLists.txt](../CMakeLists.txt) for the actual build configuration.
