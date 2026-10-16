#!/usr/bin/env python3
"""Cancel merge-queue workflow runs whose queue entry no longer exists."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
from typing import Callable, Iterable, Sequence

# Thanks to GitHub, "Managing a merge queue": each queue entry is tested on a
# temporary gh-readonly-queue/<base>/pr-<number>-<sha> branch, and GitHub deletes
# that branch when the entry merges or its group is rebuilt (for example after
# an entry ahead of it is removed). Runs for a deleted branch are not cancelled
# automatically and would still occupy runners.
# https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/configuring-pull-request-merges/managing-a-merge-queue
QUEUE_BRANCH_PREFIX = "gh-readonly-queue/"
# Thanks to GitHub's REST reference, "List workflow runs for a repository":
# the status filter accepts these unfinished states.
# https://docs.github.com/en/rest/actions/workflow-runs#list-workflow-runs-for-a-repository
UNFINISHED_STATUSES = ("queued", "in_progress", "waiting", "pending", "requested")
GH_SECONDS = 60

GhRunner = Callable[[Sequence[str]], str]


class CleanupError(Exception):
    """Listing state failed or looked inconsistent; nothing was cancelled."""


def run_gh(arguments: Sequence[str]) -> str:
    result = subprocess.run(["gh", *arguments], check=True, capture_output=True, text=True, timeout=GH_SECONDS)
    return result.stdout


def list_unfinished_queue_runs(repository: str, gh: GhRunner) -> list[tuple[int, str]]:
    """Return (run id, head branch) for every unfinished merge_group run."""
    runs: dict[int, str] = {}
    for status in UNFINISHED_STATUSES:
        output = gh([
            "api", "--paginate",
            f"repos/{repository}/actions/runs?event=merge_group&status={status}&per_page=100",
            "--jq", ".workflow_runs[] | [.id, .head_branch] | @tsv",
        ])
        for line in output.splitlines():
            if not line.strip():
                continue
            identifier, separator, branch = line.partition("\t")
            if not separator or not identifier.isdigit():
                raise CleanupError(f"Unexpected workflow run listing line: {line!r}")
            runs[int(identifier)] = branch
    return sorted(runs.items())


def list_live_queue_branches(repository: str, gh: GhRunner) -> set[str]:
    """Return the merge-queue branches that currently exist."""
    output = gh([
        "api", "--paginate", f"repos/{repository}/git/matching-refs/heads/{QUEUE_BRANCH_PREFIX}",
        "--jq", ".[].ref",
    ])
    return {line.strip().removeprefix("refs/heads/") for line in output.splitlines() if line.strip()}


def select_stale_runs(runs: Iterable[tuple[int, str]], live_branches: set[str], current_run_id: int) -> list[int]:
    """Select runs for merge-queue branches that no longer exist, never other runs or this one."""
    return [
        identifier for identifier, branch in runs
        if branch.startswith(QUEUE_BRANCH_PREFIX) and branch not in live_branches and identifier != current_run_id
    ]


def cancel_stale_runs(repository: str, gh: GhRunner, *, current_run_id: int, current_branch: str | None) -> list[int]:
    """Cancel stale merge-queue runs and return the run ids that were cancelled."""
    # List runs before branches. A listed run's branch existed when the run was
    # created, so the later branch snapshot omits it only if GitHub deleted it.
    # Runs created after the run listing are not considered at all.
    runs = list_unfinished_queue_runs(repository, gh)
    live_branches = list_live_queue_branches(repository, gh)
    if current_branch is not None and current_branch not in live_branches:
        # The triggering entry's own branch must exist; an empty or failed
        # listing would otherwise make every queue run look stale.
        raise CleanupError(f"Triggering queue branch {current_branch} is missing from the listing")
    cancelled = []
    for identifier in select_stale_runs(runs, live_branches, current_run_id):
        # Thanks to GitHub's REST reference, "Force cancel a workflow run": an
        # ordinary cancel still schedules jobs with if: always(), such as the
        # required gate, so a stale run would keep waiting for a runner. A
        # discarded entry's results are never used, so force-cancel skips them.
        # https://docs.github.com/en/rest/actions/workflow-runs#force-cancel-a-workflow-run
        try:
            gh(["api", "-X", "POST", f"repos/{repository}/actions/runs/{identifier}/force-cancel"])
        except subprocess.CalledProcessError as error:
            # Usually HTTP 409: the run finished between listing and cancelling.
            detail = (error.stderr or "").strip() or f"exit {error.returncode}"
            print(f"Could not cancel stale merge-queue run {identifier}: {detail}", file=sys.stderr)
            continue
        print(f"Cancelled stale merge-queue run {identifier}")
        cancelled.append(identifier)
    return cancelled


def main(argv: Sequence[str] | None = None, gh: GhRunner = run_gh) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)
    repository = os.environ.get("GITHUB_REPOSITORY", "")
    if repository.count("/") != 1:
        print("GITHUB_REPOSITORY must name owner/repository.", file=sys.stderr)
        return 1
    run_id = os.environ.get("GITHUB_RUN_ID", "")
    current_run_id = int(run_id) if run_id.isdigit() else 0
    current_branch = None
    if os.environ.get("GITHUB_EVENT_NAME") == "merge_group":
        current_branch = os.environ.get("GITHUB_REF", "").removeprefix("refs/heads/")
    try:
        cancelled = cancel_stale_runs(repository, gh, current_run_id=current_run_id, current_branch=current_branch)
    except (CleanupError, subprocess.SubprocessError, OSError) as error:
        print(f"Merge-queue cleanup did not cancel anything: {error}", file=sys.stderr)
        return 1
    message = f"Cancelled {len(cancelled)} stale merge-queue run(s)."
    print(message)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a", encoding="utf-8") as summary:
            summary.write(message + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
