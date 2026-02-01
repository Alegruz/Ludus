# Ludus Engine - Dependency Management

## Overview

Ludus uses automated dependency management to keep Volk and Vulkan-Headers up-to-date without manual intervention.

## Versioning Strategy

### Pinned vs. Bleeding-Edge

| Method | When to Use | Behavior |
|--------|-------------|----------|
| **CMakeLists.txt** (pinned) | Normal development | Specific, tested versions |
| **init_with_latest_volk.bat** | Testing only | Always fetches latest tag |
| **CI workflow** (automated) | Weekly | Creates PRs with updates |

### Version Formats

- **Volk**: `vulkan-sdk-X.Y.Z.W` (e.g., `vulkan-sdk-1.4.335.0`)
- **Vulkan-Headers**: `vX.Y.Z` (e.g., `v1.3.275`)

⚠️ Invalid formats will be rejected by pre-commit hooks and CI validation.

## Automated Update Process

### Weekly Dependency Check (GitHub Actions)

Every Monday at 9 AM UTC, the workflow:

1. ✅ Checks GitHub for latest releases
2. 📊 Compares against pinned versions
3. 📝 Creates a draft PR if updates available
4. 🏗️ Runs full build & test suite
5. 📋 Leaves comprehensive report in PR

**What you see:** Draft PR appears in your inbox → review changelog → merge if safe

### Version Validation

On every commit/PR:
- ✅ Version format validation
- ✅ Syntax checking
- ⚠️ Outdated version warnings (non-blocking)

## Manual Checks

### Check for Updates Locally

```bash
python tools/check_dependency_updates.py
```

Output:
```
✅ VOLK - Up-to-date (vulkan-sdk-1.4.335.0)
🆕 VULKAN-HEADERS - Update available!
   Current:  v1.3.275
   Latest:   v1.3.280
   Changelog: Bug fixes for validation layers...
```

### Auto-Update & Create Branch

```bash
python tools/check_dependency_updates.py --update-versions
```

This updates CMakeLists.txt locally—you can then create a PR manually.

## Pre-Commit Validation

The repository includes a pre-commit hook that validates version formats:

```bash
# Install hook (one-time)
python tools/setup_hooks.py

# Automatic on every commit
git commit ...  # → Validates version formats
```

If validation fails:
```
❌ CMake version validation failed:
  Invalid VOLK_TAG: 1.3.275
  Expected: vulkan-sdk-X.Y.Z.W
```

## Maintenance Workflow

### Merging Dependency Updates

1. **Review PR** from automated workflow
2. **Check changelog** for breaking changes
3. **Verify CI passes** (build + tests)
4. **Test locally** if major version bump
5. **Merge** draft PR

### If Update Breaks Build

1. Create branch from the failed PR
2. Fix incompatibilities in code
3. Document findings
4. Push fixes to PR for review

### Manual Version Bump

If you need to pin a specific version:

```cmake
# CMakeLists.txt
set(VOLK_TAG vulkan-sdk-1.4.300.0)           # Use specific tag
set(VULKAN_HEADERS_TAG v1.3.250)             # Matching headers version
```

Then:
```bash
git commit -m "chore: pin dependencies to X.Y.Z for compatibility"
```

## Troubleshooting

### CI says version format is invalid
- ❌ `VOLK_TAG 1.3.275` → ✅ `VOLK_TAG vulkan-sdk-1.3.275.0`
- ❌ `VULKAN_HEADERS_TAG 1.3.275` → ✅ `VULKAN_HEADERS_TAG v1.3.275`

### GitHub Actions workflow not running
- Check `.github/workflows/check-dependencies.yml` exists
- Verify Actions are enabled in repo settings
- Manually trigger: Actions tab → "Check & Update Dependencies" → "Run workflow"

### Local check script fails
```bash
pip install requests
python tools/check_dependency_updates.py
```

## Configuration

Edit automation schedules in `.github/workflows/check-dependencies.yml`:

```yaml
schedule:
  - cron: '0 9 * * 1'  # Monday 9 AM UTC
```

Change to your preferred day/time using [cron syntax](https://crontab.guru/).

## Disabling Automation

**To run manual-only updates:**

Comment out the schedule in `.github/workflows/check-dependencies.yml`:
```yaml
# schedule:
#   - cron: '0 9 * * 1'
```

Still can trigger manually via Actions tab.
