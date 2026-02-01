# Automated Dependency Management - Quick Start

## One-Time Setup

```bash
# 1. Install pre-commit hook (validates version formats on every commit)
python tools/setup_tools.py

# 2. Install Python dependencies (if not already installed)
pip install requests
```

## How It Works (No Manual Intervention Required)

### ✨ Automatic Weekly Check
- **Every Monday at 9 AM UTC**, GitHub Actions automatically:
  1. Checks GitHub for newer Volk/Vulkan-Headers releases
  2. Compares against your pinned versions
  3. Creates a **draft PR** if updates exist
  4. Runs full build & test suite on the PR
  5. Leaves a comprehensive report in the PR

**What you do:** Simply review the PR in your inbox → merge if safe

---

## Manual Checks (Optional)

### Check What's Available Locally
```bash
python tools/check_dependency_updates.py
```

Output example:
```
✅ VOLK - Up-to-date (vulkan-sdk-1.4.335.0)
🆕 VULKAN-HEADERS - Update available!
   Current:  v1.3.275
   Latest:   v1.4.342
```

### Update Versions Locally & Create PR
```bash
# This updates CMakeLists.txt with latest versions
python tools/check_dependency_updates.py --update-versions

# Then create and push your PR
git checkout -b deps/update-external-dependencies
git add CMakeLists.txt
git commit -m "chore: update dependencies"
git push origin deps/update-external-dependencies
```

---

## Pre-Commit Hook (Automatic Validation)

The hook prevents invalid version formats:

```bash
git commit -m "bad version"
# ❌ CMake version validation failed:
#   Invalid VOLK_TAG: 1.3.275
#   Expected: vulkan-sdk-X.Y.Z.W
```

To bypass (not recommended):
```bash
git commit --no-verify
```

---

## Status Commands

### Check Workflow Status
```bash
# In GitHub Actions tab: "Check & Update Dependencies" workflow
# Or run locally:
python tools/check_dependency_updates.py
```

### View Last Report
```bash
# GitHub Actions creates an artifact with the full report
# Check the workflow run details
```

---

## Configuration

### Change Update Schedule
Edit `.github/workflows/check-dependencies.yml`:
```yaml
schedule:
  - cron: '0 9 * * 1'  # Monday 9 AM UTC → change to your time
```

Use [crontab.guru](https://crontab.guru/) to adjust timing.

### Disable Automatic Updates
Comment out or remove the `schedule` section in `.github/workflows/check-dependencies.yml`:
```yaml
# schedule:
#   - cron: '0 9 * * 1'
```

Still can trigger manually via Actions tab.

---

## Troubleshooting

### Hook Not Running
```bash
# Re-install hook
python tools/setup_tools.py
```

### GitHub Actions Not Running
- Verify `.github/workflows/` files exist
- Check Actions are enabled in repo settings
- Manually trigger: Actions tab → Workflow → "Run workflow"

### Version Check Fails
```bash
# Ensure requests library is installed
pip install requests

# Try again
python tools/check_dependency_updates.py
```

---

## Summary

✅ **Setup:** `python tools/setup_tools.py`  
✅ **Automatic:** Runs every Monday, creates PR if updates found  
✅ **Manual:** `python tools/check_dependency_updates.py` anytime  
✅ **Validated:** Pre-commit hook prevents bad version formats  

**No more manual tracking needed!**
