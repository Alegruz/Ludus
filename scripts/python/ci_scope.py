#!/usr/bin/env python3
"""Route documentation-only changes and enforce the required native CI gate."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


# Explicitly exempt documentation tooling only; unknown paths require native CI.
DOCUMENTATION_FILES = frozenset({
    "README.md",
    "mkdocs.yml",
    ".github/workflows/wiki.yml",
    "config/doxygen_toolchain.json",
    "scripts/build-api",
    "scripts/check-wiki",
    "scripts/python/api_reference.py",
    "scripts/python/api_markdown.py",
    "scripts/python/wiki_api_hook.py",
    "scripts/python/test_api_reference.py",
    "scripts/python/test_api_markdown.py",
    "scripts/python/test_wiki.py",
})
NATIVE_JOBS = (
    "macos-compile", "formatting", "debug-runtime", "development",
    "sanitizers", "filesystem-fuzz", "threading-races", "analysis", "editor",
    "assertion-policy", "pch", "build-budget", "scripting-qualification",
)


def documentation_only(paths: list[str]) -> bool:
    if not paths:
        return False
    for path in paths:
        parts = PurePosixPath(path).parts
        if path.startswith("/") or ".." in parts:
            return False
        if path not in DOCUMENTATION_FILES and not path.startswith("docs/"):
            return False
    return True


def changed_paths(event_name: str, event: dict, root: Path) -> list[str]:
    if event_name == "pull_request":
        base = event["pull_request"]["base"]["sha"]
        head = event["pull_request"]["head"]["sha"]
        separator = "..."  # Entire PR since its merge base, not just its last commit.
    elif event_name == "push":
        base, head = event["before"], event["after"]
        separator = ".."
    else:
        raise ValueError("manual or unsupported event requires native validation")
    for sha in (base, head):
        if (
            not isinstance(sha, str)
            or not re.fullmatch(r"[0-9a-fA-F]{40}|[0-9a-fA-F]{64}", sha)
            or not sha.strip("0")
        ):
            raise ValueError("missing or invalid comparison commit")
    # Thanks to Git's git-diff documentation: disabling rename detection keeps
    # both paths, so moving an engine file into docs cannot qualify for exemption.
    # NUL-separated output also preserves filenames containing whitespace.
    # https://git-scm.com/docs/git-diff
    result = subprocess.run(
        ["git", "diff", "--name-only", "--no-renames", "-z", f"{base}{separator}{head}", "--"],
        cwd=root, check=True, capture_output=True, timeout=30,
    )
    return [os.fsdecode(path) for path in result.stdout.split(b"\0") if path]


def classify(event_name: str, event: dict, root: Path) -> tuple[bool, str]:
    try:
        paths = changed_paths(event_name, event, root)
    except (KeyError, TypeError, ValueError, OSError, subprocess.SubprocessError):
        return True, "Comparison unavailable or event requires full validation."
    if documentation_only(paths):
        return False, f"All {len(paths)} changed paths are documentation or documentation tooling."
    return True, "Engine, tooling, policy, unknown paths, or an empty diff require full validation."


def gate_failures(results: dict) -> list[str]:
    failures = []
    scope = results.get("changes", {})
    if scope.get("result") != "success":
        return ["Change classification did not succeed."]
    required = scope.get("outputs", {}).get("native_required")
    if required not in {"true", "false"}:
        return ["Change classification did not report a valid native_required output."]
    expected = "success" if required == "true" else "skipped"
    if set(results) != {"changes", *NATIVE_JOBS}:
        failures.append("Required native job inventory is incomplete or unexpected.")
    for name in NATIVE_JOBS:
        actual = results.get(name, {}).get("result")
        if actual != expected:
            failures.append(f"{name}: expected {expected}, received {actual}.")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("classify", "gate"))
    args = parser.parse_args()
    if args.command == "gate":
        failures = gate_failures(json.loads(os.environ["RESULTS"]))
        if failures:
            print("\n".join(failures), file=sys.stderr)
            return 1
        print("Required CI gate passed for the classified change scope.")
        return 0
    try:
        event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
        required, reason = classify(os.environ.get("GITHUB_EVENT_NAME", ""), event, Path.cwd())
    except (OSError, ValueError, KeyError):
        required, reason = True, "Event metadata unavailable; full validation required."
    value = "true" if required else "false"
    with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as output:
        output.write(f"native_required={value}\n")
    message = f"Native validation required: {value}. {reason}"
    print(message)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a", encoding="utf-8") as summary:
            summary.write(message + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
