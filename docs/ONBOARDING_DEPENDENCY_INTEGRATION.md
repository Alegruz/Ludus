# Onboarding & Dependency Management Integration

## Overview

The Ludus onboarding process (`init.bat`/`init.sh`) now automatically sets up **automated dependency management**. New contributors won't need to worry about tracking or updating external dependencies.

## What Happens During Onboarding

When you run the onboarding setup (`init.bat` or `init.sh`), the system:

1. ✅ Installs prerequisites (VS Code, LLVM tools, etc.)
2. ✅ Configures CMake presets
3. ✅ **Sets up dependency automation** (NEW!)
   - Installs pre-commit hook for validation
   - Records initial dependency versions
   - Enables weekly automatic update checks

## For New Team Members

After running onboarding, they automatically get:

- 🔒 **Version validation** - Pre-commit hook prevents invalid version formats
- 📦 **Automatic update PRs** - GitHub Actions checks weekly for new releases
- 📖 **Clear documentation** - Links to `DEPENDENCY_UPDATES_QUICKSTART.md`

No additional setup steps needed!

## Dependency Versions Tracked

The onboarding process records which versions were pinned when the project was initialized:

```json
{
  "role": "developer",
  "preset": "ninja_msvc-debug-vulkan",
  "dependencies": {
    "volk": "vulkan-sdk-1.4.335.0",
    "vulkan-headers": "v1.3.275"
  },
  "dependency_automation_enabled": true
}
```

This is stored in `tools/onboard/onboard_state.json` for future reference.

## Workflow for Contributors

### First-Time Setup
```bash
# Windows
init.bat

# Or Linux/macOS
./init.sh

# Automatically sets up:
# ✅ Pre-commit hooks
# ✅ Dependency automation
# ✅ Opens VS Code with configured preset
```

### Daily Development
- Work normally with pinned dependencies
- Pre-commit hook validates any edits to CMakeLists.txt
- No action needed!

### When Updates Arrive
- GitHub Actions creates a **draft PR** every Monday
- Review the changelog
- Merge if safe, skip if not
- That's it!

## For Maintainers

### Manual Update Check
Even if you modify `CMakeLists.txt` directly:

```bash
python tools/check_dependency_updates.py
```

The system validates your changes automatically on commit.

### Override Pre-Commit Validation
If needed (not recommended):
```bash
git commit --no-verify
```

### Customize Update Schedule
Edit `.github/workflows/check-dependencies.yml`:
```yaml
schedule:
  - cron: '0 9 * * 1'  # Change this to your preferred time
```

## Troubleshooting

### Pre-Commit Hook Not Running
Re-run setup:
```bash
python tools/setup_tools.py
```

### Onboarding Fails to Set Up Dependencies
- Check Python is installed: `python --version`
- Verify requests library: `pip install requests`
- Check `.github/workflows/` files exist
- Try manual setup: `python tools/setup_tools.py`

### Dependency Checker Won't Run
```bash
# Ensure you're in the repo root
cd /path/to/Ludus

# Try the checker directly
python tools/check_dependency_updates.py

# If it fails, install dependencies
pip install requests

# Try again
python tools/check_dependency_updates.py
```

## Integration Points

| Component | Integration | Purpose |
|-----------|-----------|---------|
| **init.bat/init.sh** | Calls setup scripts | Initializes automation |
| **setup_tools.py** | Installs pre-commit hook | Validates versions on commit |
| **record_dependency_versions.py** | Updates onboard state | Tracks initial versions |
| **check_dependency_updates.py** | Pre-commit validation | Prevents bad version formats |
| **.github/workflows/** | Automated checks | Weekly update scanning |
| **.ludus-initialized** | Marker file | Indicates successful onboarding |

## For CI/CD Pipelines

The onboarding automation **doesn't interfere** with CI/CD:

- CI can skip onboarding if a marker file (`.ludus-initialized`) exists
- Dependency checker validates versions in PRs automatically
- Build matrix can test multiple dependency versions if needed
- Workflows can skip version validation if running with different versions

## Next Steps for Contributors

After onboarding, point them to:

1. [DEPENDENCY_UPDATES_QUICKSTART.md](DEPENDENCY_UPDATES_QUICKSTART.md) - How dependency management works
2. [DEPENDENCY_MANAGEMENT.md](DEPENDENCY_MANAGEMENT.md) - Full policy details
3. [AUTOMATED_DEPENDENCIES_README.md](AUTOMATED_DEPENDENCIES_README.md) - System overview

They don't need to read these unless they want details—the system works automatically!

## Development Workflow Example

```bash
# New contributor
./init.bat
# ✅ Onboarding complete, dependency automation active

# Next Monday
# 📬 PR arrives: "🔄 Dependencies: Update Volk and Vulkan-Headers"
# 📖 Review changelog in PR description

# If safe to update:
# ✅ Merge PR

# If not safe:
# ❌ Close PR or wait for better timing

# If you want to manually check:
python tools/check_dependency_updates.py
# Shows current vs. latest versions
```

That's it! The system handles the rest.
