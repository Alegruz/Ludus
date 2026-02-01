#!/usr/bin/env python3
"""
Add dependency versions to onboarding state when setting up.
This creates a record of what versions were pinned when the project was initialized.
"""

import json
import re
import sys
from pathlib import Path

def get_current_versions():
    """Extract current dependency versions from CMakeLists.txt"""
    repo_root = Path(__file__).resolve().parents[2]
    cmake_file = repo_root / "CMakeLists.txt"
    
    if not cmake_file.exists():
        return {}
    
    content = cmake_file.read_text()
    versions = {}
    
    volk_match = re.search(r'set\(VOLK_TAG\s+([^\)]+)\)', content)
    if volk_match:
        versions["volk"] = volk_match.group(1).strip()
    
    headers_match = re.search(r'set\(VULKAN_HEADERS_TAG\s+([^\)]+)\)', content)
    if headers_match:
        versions["vulkan-headers"] = headers_match.group(1).strip()
    
    return versions

def update_onboard_state():
    """Update onboard state with current dependency versions"""
    repo_root = Path(__file__).resolve().parents[2]
    state_file = repo_root / "tools" / "onboard" / "onboard_state.json"
    
    try:
        if state_file.exists():
            state = json.loads(state_file.read_text())
        else:
            state = {}
        
        # Add dependency versions
        state["dependencies"] = get_current_versions()
        state["dependency_automation_enabled"] = True
        
        state_file.write_text(json.dumps(state, indent=2))
        return True
    except Exception as e:
        print(f"Error updating onboard state: {e}")
        return False

if __name__ == "__main__":
    success = update_onboard_state()
    sys.exit(0 if success else 1)
