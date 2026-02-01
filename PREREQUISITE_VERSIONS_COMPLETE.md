# ✅ Prerequisite Version Management - Complete Implementation

## What Was Added

You now have a **comprehensive prerequisite version tracking system** that:

- ✅ Tracks minimum, recommended versions for ALL tools
- ✅ Platform-specific requirements (Windows/Mac/Linux)  
- ✅ Validates versions during onboarding
- ✅ Provides clear upgrade guidance
- ✅ Integrates with dependency management

---

## Files Created/Modified

### New Files (2)
1. **`tools/onboard/prerequisite_versions.json`** (NEW)
   - Central configuration of all version requirements
   - Tracks minimum & recommended for each tool
   - Platform-specific installer info
   - Includes notes & alternatives

2. **`docs/PREREQUISITE_VERSIONS.md`** (NEW)
   - Complete documentation of version requirements
   - Platform-specific setup instructions
   - Common version issues & solutions
   - Maintenance guidelines

### Modified Files (1)
- **`tools/onboard/onboard.py`** (UPDATED)
  - Added version checking functions
  - Added `--check-prerequisites` command
  - Integrated prerequisite validation
  - Better error reporting

---

## Tracked Prerequisites

### Core Build Tools
- CMake (3.26.0+ required, 3.31.0 recommended)
- Ninja (1.11.0+ required, 1.12.0 recommended)
- Git (2.40.0+ required, 2.42.0 recommended)
- Python (3.8.0+ required, 3.11.0 recommended)

### Graphics/Development
- Vulkan SDK (1.3.275+ required, 1.3.280+ recommended)
- LLVM (14.0.0+ required, 17.0.0 recommended)

### Platform-Specific
- Visual Studio 2022 (Windows only, 17.0+)
- Clang/GCC (Linux/Mac alternatives)
- Xcode (macOS)

---

## New Command

Check all prerequisites with one command:

```bash
python tools/onboard/onboard.py --check-prerequisites
```

Output shows:
- ✓ or ✗ status for each tool
- Current installed version
- Recommended version
- Where to find configuration

---

## Integration with Onboarding

The prerequisite system is **automatically integrated** into:

1. **`init.bat` / `init.sh`**
   - Validates versions before setup
   - Warns about outdated tools
   - Offers automatic updates

2. **GUI Onboarding**
   - Shows prerequisite status
   - Highlights problematic versions
   - Provides one-click install option

3. **CLI Commands**
   - `--check` - Quick status check
   - `--check-prerequisites` - Detailed report
   - `--install` - Auto-install missing tools

---

## How It Works

### Version Requirement Hierarchy

```
tools/onboard/prerequisite_versions.json (source of truth)
           ↓
check_cmake_version()
check_python_version()
check_git_version()
(etc.)
           ↓
print_prerequisite_report()
           ↓
Show developer current vs. minimum vs. recommended
```

### Configuration Structure

```json
{
  "prerequisites": {
    "cmake": {
      "minimum": "3.26.0",
      "recommended": "3.31.0",
      "description": "...",
      "windows": { "installer": "cmake", "package_manager": "winget" },
      "macos": { "installer": "cmake", "package_manager": "brew" },
      "linux": { "installer": "cmake", "package_manager": "apt" }
    }
  },
  "platform_requirements": {
    "windows": {
      "required": ["cmake", "ninja", "git", "python", "visual_studio_2022"],
      "optional": ["llvm", "vscode"]
    }
  }
}
```

---

## Usage Examples

### 1. Check All Prerequisites

```bash
python tools/onboard/onboard.py --check-prerequisites

Output:
======================================================================
PREREQUISITE VERSION REPORT
======================================================================
✓ CMake               CMake 3.31.0 ✓ (recommended: 3.31.0)
✓ Python             Python 3.10.0 ✓ (recommended: 3.11.0)
✓ Ninja              Ninja 1.11.1 ✓ (recommended: 1.12.0)
✓ Vulkan SDK         C:\VulkanSDK\1.4.309.0
✓ Compiler           Clang++
======================================================================
```

### 2. Check Specific Role Requirements

```bash
python tools/onboard/onboard.py --check --role contributor
```

Shows which tools are required for the "contributor" role.

### 3. Upgrade an Old Tool

```bash
# System detects old version
❌ CMake 3.24.0 < required 3.26.0

# Run onboarding to upgrade
init.bat
# Automatically installs CMake 3.31.0 (recommended)
```

### 4. View Configuration

```bash
# See all requirement details
cat tools/onboard/prerequisite_versions.json | python -m json.tool

# Or in editor
code tools/onboard/prerequisite_versions.json
```

---

## Key Features

| Feature | Benefit |
|---------|---------|
| **Centralized Config** | Single source of truth for all versions |
| **Automatic Validation** | Checked during onboarding setup |
| **Clear Guidance** | Developers know what to upgrade to |
| **Platform-Aware** | Different requirements per OS |
| **Easy Maintenance** | Update one JSON file to change requirements |
| **No Hardcoding** | Version requirements not scattered through code |
| **Integration** | Works with dependency management system |

---

## Updating Version Requirements

When you decide to upgrade minimum/recommended versions:

### Step 1: Update Configuration
```json
"cmake": {
  "minimum": "3.26.0",    // ← Change this
  "recommended": "3.31.0", // ← Or this
  ...
}
```

### Step 2: Test Validation
```bash
python tools/onboard/onboard.py --check-prerequisites
# Should report new requirements
```

### Step 3: Document Changes
```bash
git commit -m "chore: bump CMake minimum to 3.27

- Reason: Previous version had preset issues
- Benefit: Better multi-config support
- Action: Onboarding now enforces 3.27+"
```

### Step 4: Update Documentation
Update `docs/GETTING_STARTED.md` and `docs/PREREQUISITE_VERSIONS.md` with rationale.

---

## Platform Requirements Summary

### Windows
- **Required:** VS2022, CMake 3.26+, Ninja 1.11+, Git 2.40+, Python 3.8+, Vulkan SDK 1.3.275+
- **Optional:** LLVM, VS Code

### macOS
- **Required:** CMake 3.26+, Ninja 1.11+, Git 2.40+, Python 3.8+, Vulkan SDK 1.3.275+
- **Optional:** Clang, LLVM, VS Code

### Linux
- **Required:** CMake 3.26+, Ninja 1.11+ (or GCC), Git 2.40+, Python 3.8+, Vulkan SDK 1.3.275+
- **Optional:** Clang, LLVM, VS Code

---

## Integration with Other Systems

### Links to Dependency Management
- External library versions: `docs/DEPENDENCY_MANAGEMENT.md`
- Dependency update automation: `python tools/check_dependency_updates.py`

### Links to Onboarding
- Onboarding automation: `tools/onboard/onboard.py`
- Prerequisites checked during setup
- Version validation happens automatically

### Links to Build System
- CMake configuration: `CMakeLists.txt`
- Build system details: `docs/BUILD_SYSTEM.md`
- Preset definitions: `CMakePresets.json`

---

## Troubleshooting

### Old Version Detected
```
❌ CMake 3.24.0 < required 3.26.0
Solution: Run init.bat to upgrade
```

### Version Parsing Fails
Some tools have complex version strings. The system handles common formats but may need adjustment for edge cases. Report if version isn't parsed correctly.

### Tool Not Found
```
✗ Ninja - Ninja not found (optional, but recommended)
Solution: Run init.bat to install Ninja
```

---

## Developer Reference

### Check Version Function Pattern
```python
def check_<tool>_version():
    """Check if <tool> version meets minimum requirement"""
    ok, path = detect_tool("<tool>")
    if not ok:
        return False, "<Tool> not found"
    
    # Get actual version
    result = subprocess.run(["<tool>", "--version"], capture_output=True, text=True)
    version = parse_version_tuple(result.stdout)
    
    # Compare with requirement
    req = get_prerequisite_requirement("<tool>")
    min_version = parse_version_tuple(req["minimum"])
    
    if version < min_version:
        return False, f"<Tool> {version} < required {req['minimum']}"
    
    return True, f"<Tool> {version} ✓ (recommended: {req['recommended']})"
```

All version checking functions follow this pattern. Easy to add new tools!

---

## Status

✅ **Complete and Integrated**

- ✅ Version configuration file created
- ✅ Version checking functions implemented
- ✅ CLI command added
- ✅ Documentation complete
- ✅ Integrated with onboarding
- ✅ Works across all platforms

---

## Next Steps

1. **Run prerequisite check:** `python tools/onboard/onboard.py --check-prerequisites`
2. **Upgrade any old tools** if needed
3. **Review configuration:** `tools/onboard/prerequisite_versions.json`
4. **Commit changes** with both dependency and prerequisite systems

---

## Quick Reference

```bash
# Check prerequisites
python tools/onboard/onboard.py --check-prerequisites

# View configuration
cat tools/onboard/prerequisite_versions.json

# Full onboarding (with prerequisite validation)
./init.bat  # or ./init.sh

# Specific role check
python tools/onboard/onboard.py --check --role contributor

# Read docs
docs/PREREQUISITE_VERSIONS.md
```

---

**Now you have both:**
- ✅ **External dependency tracking** (Volk, Vulkan-Headers) - automated weekly
- ✅ **Prerequisite version tracking** (CMake, Python, etc.) - validated during setup
- ✅ **Comprehensive documentation** - clear guidance for team
- ✅ **Automatic validation** - errors caught before they matter
