# Prerequisite Version Management

## Overview

Ludus tracks **minimum**, **recommended**, and **platform-specific** version requirements for all external dependencies in `tools/onboard/prerequisite_versions.json`. This ensures:

- ✅ Developers know exactly what versions they need
- ✅ Onboarding script can validate prerequisites
- ✅ Clear upgrade paths when needed
- ✅ Platform-specific alternatives documented

---

## Key Prerequisites

### Core Build Tools

| Tool | Minimum | Recommended | Purpose |
|------|---------|-------------|---------|
| **CMake** | 3.26.0 | 3.31.0 | Build system configuration |
| **Ninja** | 1.11.0 | 1.12.0 | Fast build executor (optional but recommended) |
| **Git** | 2.40.0 | 2.42.0 | Version control |
| **Python** | 3.8.0 | 3.11.0 | Scripting & onboarding tools |

### Graphics/Vulkan

| Tool | Minimum | Recommended | Purpose |
|------|---------|-------------|---------|
| **Vulkan SDK** | 1.3.275 | 1.3.280+ | Vulkan headers & validation |
| **LLVM** | 14.0.0 | 17.0.0 | clang-format, clang-tidy |

### Platform-Specific

| Platform | Required | Optional |
|----------|----------|----------|
| **Windows** | VS2022 (17.0+), CMake, Ninja, Git, Python, Vulkan SDK | LLVM, VS Code |
| **macOS** | CMake, Ninja, Git, Python, Vulkan SDK | LLVM, Clang, VS Code |
| **Linux** | CMake, Ninja, Git, Python, Vulkan SDK | LLVM, Clang, GCC, VS Code |

---

## Checking Prerequisites

### View Detailed Report

```bash
# Show all prerequisite versions with status
python tools/onboard/onboard.py --check-prerequisites
```

Output:
```
======================================================================
PREREQUISITE VERSION REPORT
======================================================================
✓ CMake               CMake 3.31.0 ✓ (recommended: 3.31.0)
✓ Python             Python 3.11.8 ✓ (recommended: 3.11.0)
✓ Git                Git 2.43.0 ✓ (recommended: 2.42.0)
✓ Ninja              Ninja 1.12.1 ✓ (recommended: 1.12.0)
✓ Vulkan SDK         C:\VulkanSDK\1.3.275.0\
✓ Compiler           MSVC (cl.exe)
======================================================================

✅ All prerequisites are satisfied!

For detailed version requirements, see:
  tools/onboard/prerequisite_versions.json
```

### Check Specific Roles

```bash
# Check prerequisites for contributor role
python tools/onboard/onboard.py --check --role contributor

# Check prerequisites for debugger role
python tools/onboard/onboard.py --check --role debugger
```

---

## Version Format

The prerequisite configuration (`prerequisite_versions.json`) follows this structure:

```json
{
  "prerequisites": {
    "cmake": {
      "minimum": "3.26.0",
      "recommended": "3.31.0",
      "description": "Build system generator",
      "windows": { "installer": "cmake", "package_manager": "winget" },
      "macos": { "installer": "cmake", "package_manager": "brew" },
      "linux": { "installer": "cmake", "package_manager": "apt" }
    }
  },
  "platform_requirements": {
    "windows": {
      "required": ["cmake", "ninja", "git", "python", "visual_studio_2022", "vulkan_sdk"],
      "optional": ["llvm", "vscode"]
    }
  }
}
```

---

## Platform Requirements

### Windows

**Required:**
- Visual Studio 2022 (C++ Desktop development)
- CMake 3.26+
- Git 2.40+
- Python 3.8+
- Vulkan SDK 1.3.275+
- Ninja 1.11+ (or use Visual Studio native build)

**Optional:**
- LLVM (for clang-format/clang-tidy)
- VS Code (IDE)

### macOS

**Required:**
- CMake 3.26+
- Git 2.40+
- Python 3.8+
- Vulkan SDK 1.3.275+
- Ninja 1.11+ (or Unix Makefiles)

**Optional:**
- Clang 14+
- LLVM (for tools)
- VS Code

### Linux

**Required:**
- CMake 3.26+
- Git 2.40+
- Python 3.8+
- Vulkan SDK 1.3.275+
- Ninja 1.11+ or GCC 10+

**Optional:**
- Clang 14+
- LLVM (for tools)
- VS Code

---

## Version Checking During Onboarding

When you run onboarding, it:

1. **Detects** installed tool versions
2. **Compares** against minimum requirements
3. **Warns** if versions are outdated
4. **Suggests** upgrades if recommended versions available
5. **Offers** to install missing tools

Example:
```bash
./init.bat
# Checking prerequisites...
# ⚠️ CMake 3.25.0 is below minimum 3.26.0
# ✅ Python 3.11.0 is current
# ❌ Ninja not found
# 
# Would you like to install missing tools? [Y/n]
```

---

## Updating Version Requirements

When external tools release new versions and you decide to update minimum/recommended versions:

1. Edit `tools/onboard/prerequisite_versions.json`
2. Update `minimum` and `recommended` fields
3. Update `CMakeLists.txt` if needed
4. Commit changes with rationale
5. Update `docs/GETTING_STARTED.md`

Example commit:
```
chore: bump minimum CMake to 3.27

- CMake 3.26 has known issues with presets
- 3.27 adds better support for multi-config generators
- Onboarding script now enforces 3.27+ as minimum
```

---

## Integration with Other Systems

### Dependency Management
- External library versions (Volk, Vulkan-Headers) tracked separately
- See: `docs/DEPENDENCY_MANAGEMENT.md`
- Use: `python tools/check_dependency_updates.py`

### Onboarding Script
- Validates prerequisites before installing
- Checks versions match configuration
- Provides clear upgrade paths
- Integrates version checks into GUI

### CI/CD Pipelines
- Can use prerequisite config to determine environment
- GitHub Actions can check tool versions
- Docker/container builds can pin versions

---

## Common Version Issues

### CMake Too Old
```
❌ CMake 3.24.0 < required 3.26.0
Solution: Run init.bat or install CMake 3.26+
```

### Vulkan SDK Missing
```
❌ Vulkan SDK not found
Solution: Install from https://vulkan.lunarg.com
or run: init.bat
```

### Python 2 vs 3
```
❌ Python 2.7 < required 3.8.0
Solution: Install Python 3.8+
```

### Ninja vs Make
Ninja is optional but recommended:
```
✓ Ninja found (1.12.1) ✓
OR
⚠️ Ninja not found (using Unix Makefiles instead)
```

---

## For Maintainers

### When to Update Version Requirements

1. **New Feature Requires Newer Tool**
   - Update minimum version in config
   - Document reason in commit message

2. **Security Issue in Tool Version**
   - Update minimum to patch version
   - Note severity in documentation

3. **Performance Improvement in Newer Version**
   - Update recommended version
   - Document improvement

### Example Update Workflow

```bash
# Edit configuration
vi tools/onboard/prerequisite_versions.json

# Update minimum cmake from 3.26 to 3.27
# Update recommended cmake from 3.31 to 3.32

# Test onboarding recognizes change
python tools/onboard/onboard.py --check-prerequisites

# Update documentation
vi docs/GETTING_STARTED.md
# Add note about CMake 3.27 requirement

# Commit
git add -A
git commit -m "chore: bump CMake minimum to 3.27

- Previous 3.26 had issues with multi-config builds
- 3.27 provides better preset support
- Recommended: 3.32 for latest features"
```

---

## Documentation Cross-References

- **Detailed Requirements:** See `tools/onboard/prerequisite_versions.json`
- **Setup Instructions:** See `docs/GETTING_STARTED.md`
- **External Dependencies:** See `docs/DEPENDENCY_MANAGEMENT.md`
- **Build System:** See `docs/BUILD_SYSTEM.md`

---

## Quick Commands

```bash
# Check if you meet all prerequisites
python tools/onboard/onboard.py --check-prerequisites

# Check for specific role
python tools/onboard/onboard.py --check --role contributor

# View raw configuration
cat tools/onboard/prerequisite_versions.json | python -m json.tool

# Install missing prerequisites
python tools/onboard/onboard.py --install

# Full onboarding (GUI)
python tools/onboard/onboard.py
```

---

## Next Steps

After checking prerequisites, if you have issues:

1. **Review GETTING_STARTED.md** for platform-specific instructions
2. **Run `init.bat` or `./init.sh`** to auto-install missing tools
3. **Check individual tool documentation** for version-specific issues
4. **Read BUILD_SYSTEM.md** for build troubleshooting
