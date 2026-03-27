#!/usr/bin/env python3
"""
Dependency version checker for Ludus engine.
Tracks Volk and Vulkan-Headers versions against latest available.
Usage: python tools/check_dependency_updates.py [--check-only] [--update-versions]
"""

import requests
import re
import sys
import json
from pathlib import Path
from typing import Dict, Tuple, Optional

# Repository configurations
REPOS = {
    "volk": {
        "repo": "zeux/volk",
        "url_pattern": "https://api.github.com/repos/{}/releases/latest",
        "tag_key": "tag_name",
        "changelog_key": "body",
        "version_regex": r"vulkan-sdk-(\d+\.\d+\.\d+\.\d+)",
    },
    "vulkan-headers": {
        "repo": "KhronosGroup/Vulkan-Headers",
        "url_pattern": "https://api.github.com/repos/{}/tags",
        "tag_key": "name",
        "is_tags_endpoint": True,
        "changelog_key": "body",
        "version_regex": r"^v(\d+\.\d+\.\d+)$",  # Match exact v1.3.275 format
    },
}

class DependencyChecker:
    def __init__(self, repo_root: Path):
        self.repo_root = repo_root
        self.cmake_file = repo_root / "CMakeLists.txt"
        self.onboard_dir = repo_root / "tools" / "onboard"
        self.current_versions = self._parse_current_versions()

    def _parse_current_versions(self) -> Dict[str, str]:
        """Extract current versions from CMakeLists.txt"""
        versions = {}
        cmake_content = self.cmake_file.read_text()
        
        volk_match = re.search(r'set\(VOLK_TAG\s+([^\)]+)\)', cmake_content)
        headers_match = re.search(r'set\(VULKAN_HEADERS_TAG\s+([^\)]+)\)', cmake_content)
        
        if volk_match:
            versions["volk"] = volk_match.group(1).strip()
        if headers_match:
            headers_tag = headers_match.group(1).strip()
            # Extract version from tag (v1.3.275 -> 1.3.275)
            version_match = re.search(r'v(\d+\.\d+\.\d+)', headers_tag)
            if version_match:
                versions["vulkan-headers"] = version_match.group(1)
            else:
                versions["vulkan-headers"] = headers_tag
        
        return versions

    def get_latest_versions(self) -> Dict[str, Dict]:
        """Fetch latest versions from GitHub for all dependencies"""
        results = {}
        
        for dep_name, config in REPOS.items():
            try:
                url = config["url_pattern"].format(config["repo"])
                response = requests.get(url, timeout=10)
                response.raise_for_status()
                data = response.json()
                
                # Handle tags endpoint vs releases endpoint
                if config.get("is_tags_endpoint"):
                    # For tags endpoint, find the latest matching tag
                    if isinstance(data, list):
                        matching_tags = [
                            d.get(config["tag_key"]) 
                            for d in data 
                            if re.search(config["version_regex"], d.get(config["tag_key"], ""))
                        ]
                        tag = matching_tags[0] if matching_tags else "unknown"
                        release_url = f"https://github.com/{config['repo']}/releases/tag/{tag}"
                        changelog = ""
                    else:
                        tag = "unknown"
                        release_url = ""
                        changelog = ""
                else:
                    # For releases endpoint
                    tag = data.get(config["tag_key"], "unknown")
                    changelog = data.get(config["changelog_key"], "")[:500]
                    release_url = data.get("html_url", "")
                
                # Extract version number
                version_match = re.search(config["version_regex"], tag)
                version = version_match.group(1) if version_match else tag
                
                results[dep_name] = {
                    "tag": tag,
                    "version": version,
                    "changelog": changelog,
                    "url": release_url,
                }
            except Exception as e:
                results[dep_name] = {"error": str(e)}
        
        return results

    def compare_versions(self, current: str, latest: str) -> bool:
        """Check if latest version is newer than current"""
        try:
            # Parse version tuples for comparison
            def parse_version(v: str) -> Tuple[int, ...]:
                v_clean = re.sub(r"[^\d.]", "", v)
                return tuple(map(int, v_clean.split(".")))
            
            return parse_version(latest) > parse_version(current)
        except:
            return False

    def generate_report(self, latest_versions: Dict) -> str:
        """Generate a comprehensive update report"""
        report = []
        report.append("=" * 70)
        report.append("LUDUS ENGINE - DEPENDENCY UPDATE REPORT")
        report.append("=" * 70)
        report.append("")
        
        has_updates = False
        
        for dep in ["volk", "vulkan-headers"]:
            current = self.current_versions.get(dep, "unknown")
            latest_info = latest_versions.get(dep, {})
            
            if "error" in latest_info:
                report.append(f"❌ {dep.upper()}")
                report.append(f"   Error: {latest_info['error']}")
                report.append("")
                continue
            
            latest = latest_info.get("tag", "unknown")
            is_newer = self.compare_versions(current, latest)
            
            status = "🆕 UPDATE AVAILABLE" if is_newer else "✅ Up-to-date"
            report.append(f"{status} - {dep.upper()}")
            report.append(f"   Current:  {current}")
            report.append(f"   Latest:   {latest}")
            
            if latest_info.get("changelog"):
                changelog_lines = latest_info["changelog"].split("\n")[:3]
                report.append(f"   Changelog: {changelog_lines[0][:60]}...")
            
            if latest_info.get("url"):
                report.append(f"   Release:  {latest_info['url']}")
            
            report.append("")
            if is_newer:
                has_updates = True
        
        report.append("=" * 70)
        if has_updates:
            report.append("🔄 Updates are available! Run with --update-versions to create PR branch.")
        else:
            report.append("✨ All dependencies are current!")
        report.append("=" * 70)
        
        return "\n".join(report)

    def suggest_cmake_update(self, latest_versions: Dict) -> Optional[str]:
        """Generate CMakeLists.txt update"""
        cmake_content = self.cmake_file.read_text()
        updated = cmake_content
        
        # Update VOLK_TAG
        if "volk" in latest_versions and "error" not in latest_versions["volk"]:
            new_tag = latest_versions["volk"]["tag"]
            old_pattern = r'set\(VOLK_TAG\s+[^\)]+\)'
            updated = re.sub(old_pattern, f'set(VOLK_TAG {new_tag})', updated)
        
        # Update VULKAN_HEADERS_TAG
        if "vulkan-headers" in latest_versions and "error" not in latest_versions["vulkan-headers"]:
            new_tag = latest_versions["vulkan-headers"]["tag"]
            old_pattern = r'set\(VULKAN_HEADERS_TAG\s+[^\)]+\)'
            updated = re.sub(old_pattern, f'set(VULKAN_HEADERS_TAG {new_tag})', updated)
        
        return updated if updated != cmake_content else None

    def run(self, check_only: bool = True) -> int:
        """Main execution"""
        print("Fetching latest dependency versions...")
        latest_versions = self.get_latest_versions()
        report = self.generate_report(latest_versions)
        print(report)
        
        # Check if updates are available
        has_updates = any(
            self.compare_versions(
                self.current_versions.get(dep, "0"),
                latest_versions.get(dep, {}).get("tag", "0")
            )
            for dep in ["volk", "vulkan-headers"]
        )
        
        if has_updates and not check_only:
            print("\n📝 Suggested CMake update:")
            print("-" * 70)
            updated_cmake = self.suggest_cmake_update(latest_versions)
            if updated_cmake:
                self.cmake_file.write_text(updated_cmake)
                print("✅ CMakeLists.txt updated!")
                return 0
            else:
                print("❌ Failed to generate update")
                return 1
        
        return 0 if not has_updates else 1


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(
        description="Check and update external dependencies for Ludus engine"
    )
    parser.add_argument(
        "--check-only",
        action="store_true",
        default=True,
        help="Only check for updates (default)"
    )
    parser.add_argument(
        "--update-versions",
        action="store_true",
        help="Update CMakeLists.txt with latest versions"
    )
    
    args = parser.parse_args()
    repo_root = Path(__file__).parent.parent
    
    checker = DependencyChecker(repo_root)
    exit_code = checker.run(check_only=not args.update_versions)
    sys.exit(exit_code)
