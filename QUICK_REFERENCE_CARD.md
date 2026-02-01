# 🚀 Ludus Dependency Management - Quick Reference Card

## Your New Superpower: Zero-Maintenance Dependency Updates

```
┌─────────────────────────────────────────────────────────────┐
│  EVERY MONDAY AT 9 AM UTC                                   │
├─────────────────────────────────────────────────────────────┤
│  GitHub Actions automatically:                              │
│  1. Checks GitHub for new releases                          │
│  2. Compares with your pinned versions                      │
│  3. Creates draft PR if updates exist                       │
│  4. Runs full build & test suite                            │
│  5. Leaves changelog report                                 │
│                                                              │
│  💬 You get notified → Review → Merge when safe             │
└─────────────────────────────────────────────────────────────┘
```

---

## What You Need to Know

### ✅ It's Automatic
- No configuration needed beyond setup
- Runs every Monday at 9 AM UTC
- Creates PRs only when updates are available

### ✅ It's Safe
- Full CI/build tests before you see it
- Draft PRs (nothing auto-merges)
- Pre-commit hook prevents bad versions locally

### ✅ It's Simple
- Review changelog in PR
- Merge if good, skip if not
- Done!

---

## Three Commands You Might Use

```bash
# 1. Setup (one-time, already done)
python tools/setup_tools.py

# 2. Check manually (any time)
python tools/check_dependency_updates.py

# 3. Update & create branch (optional)
python tools/check_dependency_updates.py --update-versions
```

That's it!

---

## What Gets Checked

| Dependency | Current | Checked By |
|------------|---------|-----------|
| **Volk** | vulkan-sdk-1.4.335.0 | GitHub API weekly |
| **Vulkan-Headers** | v1.3.275 | GitHub API weekly |

---

## When Something Goes Wrong

### ❌ Pre-commit hook rejects my commit
```
❌ CMake version validation failed:
  Invalid VOLK_TAG: 1.3.275
  Expected: vulkan-sdk-X.Y.Z.W
```
**Fix:** Use the correct format: `vulkan-sdk-1.4.335.0`

### ❌ Update PR has build failures
**Action:** Create a branch from the PR, fix issues, push fixes back to PR

### ❌ GitHub Actions not running
**Check:**
- Are workflows in `.github/workflows/`?
- Are Actions enabled in repo settings?
- Can manually trigger: Actions tab → Workflow → "Run workflow"

---

## Team Communication

When a dependency update PR arrives:
1. 📨 Team gets notified
2. 💬 Everyone can see the changelog
3. 👥 Team can discuss in PR comments
4. ✅ Maintainer merges when safe

**Team stays in sync, decisions are transparent.**

---

## Documentation

| Need | Read |
|------|------|
| **Quick start** | `docs/DEPENDENCY_UPDATES_QUICKSTART.md` |
| **Full policy** | `docs/DEPENDENCY_MANAGEMENT.md` |
| **How it works** | `docs/AUTOMATED_DEPENDENCIES_README.md` |
| **Onboarding integration** | `docs/ONBOARDING_DEPENDENCY_INTEGRATION.md` |
| **System overview** | `DEPENDENCY_AUTOMATION_COMPLETE.md` |

---

## Mental Load Reduction

### Before
- ❓ "Are my dependencies outdated?"
- ❓ "When did that new version drop?"
- ❓ "Should we update now or wait?"
- ❓ "What changed in the new version?"
- ❓ "Is it compatible with our code?"
- 😰 "What if I forget to check?"

### After
- ✅ System reminds you every Monday
- ✅ PR shows you what changed
- ✅ Full CI validates compatibility
- ✅ You decide when to merge
- ✅ Team sees discussions
- ✅ Zero manual tracking

---

## Status

✅ **All systems operational**
- Pre-commit hook: Active
- GitHub Actions: Ready
- Documentation: Complete
- Onboarding: Integrated

🎯 **You're all set!**

---

## One More Thing

The system prevents this:
```
Developer commits:
  set(VOLK_TAG 1.3.275)        ❌ WRONG FORMAT

Git: "Hey! That's not valid!"
  Expected: vulkan-sdk-X.Y.Z.W

Developer fixes it:
  set(VOLK_TAG vulkan-sdk-1.3.275.0)   ✅ RIGHT

Git: "Perfect, commit accepted!"
```

**Quality assured before it ever leaves your machine.**

---

**Enjoy your newfound freedom from dependency tracking!** 🎉
