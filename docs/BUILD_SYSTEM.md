# Build System & CI Integration

This document explains the Ludus build philosophy, auto-install convenience feature, and how to integrate safely into CI/CD environments.

## Philosophy: Developer-Friendly with CI Flexibility

The Ludus build system follows these principles:

1. **Auto-install development tools for convenience**
   - clang-format, clang-tidy are auto-installed during `cmake --preset ...` if missing.
   - Uses Chocolatey (Windows), Homebrew (macOS), or apt (Linux).
   - Reduces setup friction for newcomers and local development.

2. **Optional, graceful fallbacks**
   - If auto-install fails or is disabled, the build **succeeds anyway**—only formatting and analysis targets are unavailable.
   - CI environments can disable auto-install by setting `ENABLE_CLANG_TIDY=OFF` or by pre-installing tools.

3. **Stable external dependencies**
   - mimalloc is pinned to a stable version (e.g., `v2.1.2`) in `CMakeLists.txt`, not `main` branch.
   - This ensures reproducible builds and avoids breaking changes.

4. **Clear, helpful messages**
   - When auto-install attempts are made, CMake prints what it's doing.
   - If installation fails, messages guide users to manual install steps or indicate graceful degradation.

## Development Setup

### For Local Development (Auto-Install)

Just configure and build. CMake will auto-install LLVM tools if needed:

```powershell
cmake --preset ninja_msvc-debug
cmake --build --preset ninja_msvc-debug -t LudusEditor
```

CMake will attempt to install clang-format/clang-tidy via Chocolatey, Homebrew, or apt. On Windows, if Chocolatey isn't available, it will try the PowerShell installer. If all auto-installs fail, the build still succeeds—you just won't have formatting targets.

### Manual Tool Installation (No Auto-Install)

If you prefer to control tool installation or are in a restricted environment, disable auto-install and install manually:

**Windows:**
```powershell
# Manually install first
powershell -ExecutionPolicy Bypass -File tools/install-llvm.ps1
# or
choco install llvm

# Then configure with auto-install disabled (optional, but explicit)
cmake --preset ninja_msvc-debug -DENABLE_CLANG_TIDY=ON
```

**macOS:**
```bash
brew install llvm
cmake --preset ninja_clang-debug
```

**Linux (Debian/Ubuntu):**
```bash
sudo apt-get install clang-tools
cmake --preset ninja_clang-debug
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
- The LLVM install step is optional. If skipped, the build still succeeds.
- Formatting check is optional (wrapped in `continue-on-error: true`).
- Core tests always run.

### Docker/Linux CI Example

```dockerfile
FROM ubuntu:22.04

# Install essentials
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git

# Optional: install LLVM tools for analysis
RUN apt-get install -y clang-tools

WORKDIR /ludus
COPY . .

# Configure and build
RUN cmake --preset ninja_clang-debug
RUN cmake --build --preset ninja_clang-debug -t LudusEditor
RUN cmake --build --preset ninja_clang-debug -t LudusTests

# Run tests
RUN ./build/bin/LudusTests

# Optional: format check
RUN cmake --build --preset ninja_clang-debug -t format-check || echo "Format check skipped"
```

### Disable Auto-Install in CI

For CI environments, disable auto-install to ensure builds are deterministic and don't depend on package manager availability:

```bash
# Windows CI: disable auto-install, assume tools are already present
cmake --preset ninja_msvc-debug -DENABLE_CLANG_TIDY=OFF

# Or pre-install tools, then auto-install doesn't matter (tools already found)
choco install llvm
cmake --preset ninja_msvc-debug
```

Alternatively, pre-install tools in your CI image/container:

```dockerfile
# Pre-install tools so CMake finds them immediately
RUN apt-get update && apt-get install -y clang-tools ninja-build cmake

WORKDIR /ludus
COPY . .

# Configure and build (tools already present, auto-install skipped)
RUN cmake --preset ninja_clang-debug
RUN cmake --build --preset ninja_clang-debug -t LudusEditor
```

## FAQ

**Q: Will CMake auto-install tools every time?**  
A: No. It checks first with `find_program()`. If tools are already found, auto-install is skipped. Only missing tools trigger install attempts.

**Q: Can I disable auto-install?**  
A: Yes. Set `ENABLE_CLANG_TIDY=OFF` to skip clang-tidy auto-install. Pre-installing tools disables the auto-install trigger for those tools.

**Q: What if auto-install fails?**  
A: The build succeeds. CMake prints a message indicating why the install failed. You can manually install LLVM afterwards and reconfigure.

**Q: Can I use sanitizers in CI?**  
A: Yes, on Linux with Clang/GCC. Use a sanitizer-enabled preset like `ninja_clang-relwithdebinfo`. On Windows, MSVC ASan is disabled (use Clang preset instead).

**Q: Why pin mimalloc if it's a moving target?**  
A: Pinned versions ensure reproducible builds. If you need a newer mimalloc, update the `GIT_TAG` in `CMakeLists.txt` and test thoroughly before merging.

## Stable External Dependency Versions

Keep these pinned versions updated as you test and validate new releases:

- **mimalloc**: Currently `v2.1.2`. Check https://github.com/microsoft/mimalloc/releases for new stable versions.

## Related

- See [GETTING_STARTED.md](GETTING_STARTED.md) for local setup.
- See [CMakeLists.txt](../CMakeLists.txt) for the actual build configuration.
