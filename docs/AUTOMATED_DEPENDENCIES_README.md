# 🤖 Ludus Engine - Automated Dependency System

## What's New

You now have a **completely automated dependency management system** that requires zero manual intervention.

## 🎯 Problem Solved

**Before:** You had to manually track when Volk and Vulkan-Headers needed updates  
**After:** The system automatically checks weekly and creates PRs when updates are available

## 📋 What Was Added

### 1. **Dependency Checker Script** (`tools/check_dependency_updates.py`)
- Fetches latest versions from GitHub
- Compares against pinned versions in CMakeLists.txt
- Can auto-update versions locally
- Works offline (cached results)

### 2. **GitHub Actions Workflows**
- **`check-dependencies.yml`** - Runs every Monday at 9 AM UTC
  - Checks for updates
  - Creates draft PR if updates found
  - Runs full build & test on the PR
  - Leaves detailed changelog report
  
- **`dependency-version-check.yml`** - Runs on every commit/PR
  - Validates version format correctness
  - Warns about outdated versions (non-blocking)

### 3. **Pre-Commit Hook** (`tools/hooks/pre-commit`)
- Prevents invalid version formats from being committed
- Runs automatically on `git commit`
- Can be bypassed with `--no-verify` if needed

### 4. **Documentation**
- **`docs/DEPENDENCY_MANAGEMENT.md`** - Detailed policy & config
- **`docs/DEPENDENCY_UPDATES_QUICKSTART.md`** - Quick reference for you

### 5. **Setup Script** (`tools/setup_tools.py`)
- Installs pre-commit hook
- One-time setup: `python tools/setup_tools.py`

---

## 🚀 Getting Started (5 minutes)

### Step 1: One-Time Setup
```bash
# In repository root
python tools/setup_tools.py

# Install Python dependencies
pip install requests
```

### Step 2: That's It!
The system is now active. Automatic updates will appear in your PRs every Monday.

---

## 📊 How It Works

```
Every Monday 9 AM UTC
         ↓
GitHub Actions checks GitHub API
         ↓
Compares against CMakeLists.txt
         ↓
Updates available?
         ├─ NO  → Done ✅
         └─ YES → Create draft PR 📝
              ├─ Run full build & tests
              ├─ Leave changelog report
              └─ Awaiting your review
```

**You see:** A draft PR in your inbox  
**You do:** Review changelog → merge if safe  
**System does:** Everything else automatically

---

## 💻 Manual Commands (Optional)

Check for updates anytime:
```bash
python tools/check_dependency_updates.py
```

Auto-update locally:
```bash
python tools/check_dependency_updates.py --update-versions
```

---

## 🔧 Configuration

### Change Check Schedule
Edit `.github/workflows/check-dependencies.yml`:
```yaml
schedule:
  - cron: '0 9 * * 1'  # Monday 9 AM UTC
```
(Use [crontab.guru](https://crontab.guru/) to adjust)

### Disable Automatic Updates
Remove/comment out the `schedule:` section in `.github/workflows/check-dependencies.yml`

---

## 🛡️ Version Format Enforcement

The system enforces correct version formats:

✅ **Valid:**
- Volk: `vulkan-sdk-1.4.335.0`
- Vulkan-Headers: `v1.3.275`

❌ **Invalid (will be caught by pre-commit):**
- `1.3.275` (missing prefix)
- `vulkan-sdk-1.3.275` (wrong Volk format)

---

## 📝 File Structure

```
.github/workflows/
  ├─ check-dependencies.yml           # Weekly automatic check
  └─ dependency-version-check.yml     # Commit validation

tools/
  ├─ check_dependency_updates.py      # Main checker script
  ├─ setup_tools.py                   # One-time setup
  └─ hooks/
     └─ pre-commit                    # Format validator

docs/
  ├─ DEPENDENCY_MANAGEMENT.md         # Full policy doc
  └─ DEPENDENCY_UPDATES_QUICKSTART.md # Quick reference
```

---

## ✨ Key Benefits

1. **Zero Manual Effort** - Automation handles everything
2. **Safe Updates** - Full CI/build tests run on PRs first
3. **Transparent** - Detailed changelogs in every PR
4. **Quality Guaranteed** - Pre-commit hook prevents bad commits
5. **Flexible** - Can disable automation or run manual checks
6. **Team-Friendly** - Everyone sees update PRs, can weigh in

---

## 🔍 First Run Checklist

- [ ] Run `python tools/setup_tools.py`
- [ ] Run `pip install requests`
- [ ] Run `python tools/check_dependency_updates.py` (verify it works)
- [ ] Verify `.github/workflows/` files exist
- [ ] Commit changes: `git add -A && git commit -m "chore: setup dependency automation"`
- [ ] Push to repo
- [ ] Watch for first automatic PR on Monday!

---

## ❓ FAQ

**Q: Will it auto-merge updates?**  
A: No, it creates draft PRs for you to review. You decide when to merge.

**Q: What if I want to skip an update?**  
A: Simply don't merge the PR, or close it. Next week's check will offer it again.

**Q: Can I update manually?**  
A: Yes! Run `python tools/check_dependency_updates.py --update-versions` anytime.

**Q: What if the build breaks after an update?**  
A: The PR shows you the changelog. You can revert, fix code, or adjust the tag.

**Q: How often do I need to check?**  
A: Never! The system checks automatically every Monday.

---

## 📞 Support

Refer to:
- [DEPENDENCY_MANAGEMENT.md](DEPENDENCY_MANAGEMENT.md) - Full documentation
- [DEPENDENCY_UPDATES_QUICKSTART.md](DEPENDENCY_UPDATES_QUICKSTART.md) - Quick reference
- [check_dependency_updates.py](../../tools/check_dependency_updates.py) - Source code (well-commented)
