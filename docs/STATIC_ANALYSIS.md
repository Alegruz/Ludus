# Static Analysis Setup

Status: draft
Owner: maintainers
Last updated: 2026-01-26


## Overview

The project now has fully automated clang-tidy and clang-format integration with automatic installation and PATH configuration.

## Automatic Setup

Everything is handled automatically when you run CMake configure:

1. **Auto-detection**: CMake checks for clang-tidy and clang-format
2. **Auto-installation**: If missing, runs `tools/install-llvm.ps1` (Windows) or package manager (Linux/macOS)
3. **PATH configuration**: LLVM tools are added to system PATH permanently (requires admin/sudo)
4. **CMake integration**: Tools are configured and ready to use

## How It Runs

### clang-tidy (Automatic)

**Runs automatically on every build** when `ENABLE_CLANG_TIDY=ON` (default).

```bash
# Configure (one-time)
cmake --preset ninja_msvc-debug

# Build - clang-tidy runs automatically on each file
cmake --build out/build/ninja_msvc-debug
```

clang-tidy checks are controlled by [.clang-tidy](.clang-tidy) and run during compilation:
- Performance issues
- Modernization suggestions
- Code style violations
- Bug-prone patterns
- C++ Core Guidelines compliance

**Example warnings you'll see:**
```
warning: variable 'kSomeConstant' is not used [clang-diagnostic-unused-variable]
warning: invalid case style for variable 'kSomeConstant' [readability-identifier-naming]
```

### clang-format (Manual Targets)

**Runs manually** via custom CMake targets:

```bash
# Check formatting (dry-run, fails on violations)
cmake --build out/build/ninja_msvc-debug --target format-check

# Fix formatting (applies changes in-place)
cmake --build out/build/ninja_msvc-debug --target format-fix
```

clang-format uses [.clang-format](.clang-format) configuration.

## Configuration Files

### .clang-tidy
Controls clang-tidy checks and naming conventions. Key sections:
- `Checks:` - enabled/disabled check patterns
- `CheckOptions:` - naming rules (CamelCase, camelBack, prefixes, etc.)

### .clang-format
Controls code formatting style (indentation, braces, spacing, etc.).

### CMakeLists.txt
- Sets `CMAKE_CXX_CLANG_TIDY` for automatic integration
- Creates `format-check` and `format-fix` targets
- Enables `CMAKE_EXPORT_COMPILE_COMMANDS` for IDE integration

## Disabling Checks

### Disable globally
```bash
cmake --preset ninja_msvc-debug -DENABLE_CLANG_TIDY=OFF
```

### Disable specific checks
Edit `.clang-tidy` to add to the `Checks:` list:
```yaml
Checks: >
  clang-diagnostic-*,
  -readability-identifier-length,  # Disable this check
```

### Disable inline (source code)
```cpp
// NOLINTNEXTLINE(readability-identifier-naming)
constexpr uint32_t kSomeConstant = 4;
```

## Troubleshooting

### "clang-tidy not found"
- Run `cmake --preset <preset>` again - auto-installer should run
- Manually run `tools/install-llvm.ps1` as Administrator (Windows)
- Restart VS Code after installation for PATH changes

### "compilation database not found"
- Ensure `CMAKE_EXPORT_COMPILE_COMMANDS=ON` (already set)
- Reconfigure: delete cache and run `cmake --preset <preset>` again

### Tools in PATH but CMake doesn't find them
- Restart VS Code/terminal after installation
- Check: `clang-tidy --version` and `clang-format --version`
- System PATH should include `C:\Program Files\LLVM\bin` (Windows)

## CI Integration

Add to `.github/workflows/build.yml`:

```yaml
- name: Run clang-tidy
  run: cmake --build build --target all  # clang-tidy runs automatically

- name: Check formatting
  run: cmake --build build --target format-check
```

## Best Practices

1. **Fix warnings incrementally** - don't disable checks to silence warnings
2. **Run format-fix before commits** - keeps code style consistent
3. **Use NOLINT sparingly** - only for false positives
4. **Keep .clang-tidy updated** - review and adjust rules as project evolves
