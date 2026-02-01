#!/usr/bin/env python3
"""
Setup script for development tools and hooks.
Run once: python tools/setup_tools.py
"""

import shutil
import sys
from pathlib import Path

def setup_pre_commit_hook():
    """Install pre-commit hook"""
    repo_root = Path(__file__).parent.parent
    hooks_dir = repo_root / ".git" / "hooks"
    hook_file = hooks_dir / "pre-commit"
    hook_source = repo_root / "tools" / "hooks" / "pre-commit"
    
    if not hooks_dir.exists():
        print("[FAIL] .git/hooks directory not found. Are you in a git repository?")
        return False
    
    try:
        shutil.copy(hook_source, hook_file)
        hook_file.chmod(0o755)
        print(f"[OK] Pre-commit hook installed: {hook_file}")
        return True
    except Exception as e:
        print(f"[FAIL] Failed to install pre-commit hook: {e}")
        return False

def setup_vscode_tasks():
    """Add VSCode tasks for dependency checking"""
    repo_root = Path(__file__).parent.parent
    vscode_tasks = repo_root / ".vscode" / "tasks.json"
    
    if not vscode_tasks.exists():
        print("[INFO] .vscode/tasks.json not found, skipping VSCode tasks setup")
        return True
    
    print("[INFO] VSCode tasks setup (manual)")
    print("   Add this task to .vscode/tasks.json:")
    print("""
    {
        "label": "Check Dependencies",
        "type": "shell",
        "command": "python",
        "args": ["tools/check_dependency_updates.py"],
        "group": "test",
        "presentation": {
            "reveal": "always",
            "panel": "new"
        }
    }
    """)
    return True

def main():
    print("Ludus Engine - Developer Tools Setup")
    print("=" * 50)
    
    if setup_pre_commit_hook():
        print("[OK] Setup complete!")
        return 0
    else:
        print("[WARNING] Setup had issues")
        return 1

if __name__ == "__main__":
    sys.exit(main())
