"""Regression coverage for cancelling runs of discarded merge-queue entries."""
from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest import mock

import merge_queue_cleanup as cleanup

REPOSITORY = "owner/repo"
LIVE = "gh-readonly-queue/main/pr-273-4a227ed9cbdd594e51abbc9a4bead0fe4d4d2b0e"
STALE = "gh-readonly-queue/main/pr-271-4a227ed9cbdd594e51abbc9a4bead0fe4d4d2b0e"
REBUILT = "gh-readonly-queue/main/pr-273-09f8d80eab56affa21a03ee723045bd44fff526b"


class FakeGh:
    """Serve listings by status and record every gh invocation in order."""

    def __init__(self, runs_by_status, live_branches, failing_cancels=()):
        self.runs_by_status = runs_by_status
        self.live_branches = live_branches
        self.failing_cancels = set(failing_cancels)
        self.calls = []

    def __call__(self, arguments):
        self.calls.append(list(arguments))
        if "-X" in arguments:
            identifier = int(arguments[-1].split("/")[-2])
            if identifier in self.failing_cancels:
                raise subprocess.CalledProcessError(1, "gh", stderr="HTTP 409: Cannot cancel a completed run")
            return ""
        endpoint = arguments[2]
        if "/actions/runs?" in endpoint:
            status = re.search(r"status=([a-z_]+)", endpoint).group(1)
            return "".join(f"{identifier}\t{branch}\n" for identifier, branch in self.runs_by_status.get(status, ()))
        if "/git/matching-refs/heads/" in endpoint:
            return "".join(f"refs/heads/{branch}\n" for branch in self.live_branches)
        raise AssertionError(f"unexpected gh call {arguments}")

    def cancelled(self):
        return [int(call[-1].split("/")[-2]) for call in self.calls if "-X" in call]


class SelectionTests(unittest.TestCase):
    def test_only_runs_for_missing_queue_branches_are_stale(self):
        runs = [(1, LIVE), (2, STALE), (3, REBUILT), (4, "main"), (5, "codex/feature")]
        self.assertEqual(cleanup.select_stale_runs(runs, {LIVE}, current_run_id=0), [2, 3])

    def test_the_current_run_is_never_selected(self):
        self.assertEqual(cleanup.select_stale_runs([(7, STALE)], set(), current_run_id=7), [])


class CancellationTests(unittest.TestCase):
    def setUp(self):
        patcher = mock.patch("sys.stdout")
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_lists_runs_before_branches_and_cancels_only_stale_runs(self):
        gh = FakeGh({"queued": [(1, LIVE), (2, STALE)], "in_progress": [(3, REBUILT), (2, STALE)]}, [LIVE])
        cancelled = cleanup.cancel_stale_runs(REPOSITORY, gh, current_run_id=0, current_branch=LIVE)
        self.assertEqual(cancelled, [2, 3])
        self.assertEqual(gh.cancelled(), [2, 3])
        # An ordinary cancel would still schedule the always() gate job of a stale run.
        self.assertTrue(all(call[-1].endswith("/force-cancel") for call in gh.calls if "-X" in call))
        # A run created after the run listing would otherwise look stale.
        kinds = ["runs" if "/actions/runs?" in call[2] else "refs" for call in gh.calls if "-X" not in call]
        self.assertEqual(kinds, ["runs"] * len(cleanup.UNFINISHED_STATUSES) + ["refs"])

    def test_every_unfinished_status_is_listed(self):
        gh = FakeGh({}, [LIVE])
        cleanup.cancel_stale_runs(REPOSITORY, gh, current_run_id=0, current_branch=None)
        statuses = [re.search(r"status=([a-z_]+)", call[2]).group(1) for call in gh.calls if "/actions/runs?" in call[2]]
        self.assertEqual(statuses, list(cleanup.UNFINISHED_STATUSES))

    def test_missing_triggering_branch_aborts_without_cancelling(self):
        # An empty or failed branch listing must not make every queue run stale.
        gh = FakeGh({"queued": [(1, LIVE), (2, STALE)]}, [])
        with self.assertRaises(cleanup.CleanupError):
            cleanup.cancel_stale_runs(REPOSITORY, gh, current_run_id=0, current_branch=LIVE)
        self.assertEqual(gh.cancelled(), [])

    def test_a_run_that_already_finished_does_not_stop_the_cleanup(self):
        gh = FakeGh({"queued": [(2, STALE), (3, REBUILT)]}, [LIVE], failing_cancels=[2])
        with mock.patch("sys.stderr"):
            cancelled = cleanup.cancel_stale_runs(REPOSITORY, gh, current_run_id=0, current_branch=None)
        self.assertEqual(cancelled, [3])
        self.assertEqual(gh.cancelled(), [2, 3])

    def test_malformed_listing_aborts_without_cancelling(self):
        gh = FakeGh({}, [LIVE])
        gh.runs_by_status = {"queued": [("not-a-number", STALE)]}
        with self.assertRaises(cleanup.CleanupError):
            cleanup.cancel_stale_runs(REPOSITORY, gh, current_run_id=0, current_branch=None)
        self.assertEqual(gh.cancelled(), [])


class CommandLineTests(unittest.TestCase):
    def run_main(self, environment, gh):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        summary = Path(directory.name) / "summary"
        with mock.patch.dict(os.environ, {**environment, "GITHUB_STEP_SUMMARY": str(summary)}, clear=True), \
                mock.patch("sys.stdout"), mock.patch("sys.stderr"):
            status = cleanup.main([], gh=gh)
        return status, summary.read_text(encoding="utf-8") if summary.exists() else ""

    def test_merge_group_event_cancels_stale_runs_and_reports_them(self):
        gh = FakeGh({"queued": [(1, LIVE), (2, STALE), (9, STALE)]}, [LIVE])
        status, summary = self.run_main({
            "GITHUB_REPOSITORY": REPOSITORY, "GITHUB_RUN_ID": "9", "GITHUB_EVENT_NAME": "merge_group",
            "GITHUB_REF": f"refs/heads/{LIVE}",
        }, gh)
        self.assertEqual(status, 0)
        self.assertEqual(gh.cancelled(), [2])
        self.assertIn("Cancelled 1 stale merge-queue run(s).", summary)

    def test_merge_group_event_whose_branch_is_missing_fails_without_cancelling(self):
        gh = FakeGh({"queued": [(2, STALE)]}, [])
        status, _ = self.run_main({
            "GITHUB_REPOSITORY": REPOSITORY, "GITHUB_EVENT_NAME": "merge_group", "GITHUB_REF": f"refs/heads/{LIVE}",
        }, gh)
        self.assertEqual(status, 1)
        self.assertEqual(gh.cancelled(), [])

    def test_manual_dispatch_skips_the_triggering_branch_check(self):
        gh = FakeGh({"queued": [(2, STALE)]}, [])
        status, _ = self.run_main({"GITHUB_REPOSITORY": REPOSITORY, "GITHUB_EVENT_NAME": "workflow_dispatch"}, gh)
        self.assertEqual(status, 0)
        self.assertEqual(gh.cancelled(), [2])

    def test_missing_repository_fails_before_calling_gh(self):
        gh = FakeGh({}, [])
        status, _ = self.run_main({}, gh)
        self.assertEqual(status, 1)
        self.assertEqual(gh.calls, [])


class WorkflowTests(unittest.TestCase):
    def test_cleanup_runs_for_queue_groups_with_permission_to_cancel(self):
        workflow = (Path(__file__).resolve().parents[2] / ".github/workflows/merge-queue-cleanup.yml").read_text()
        triggers = workflow.split("\npermissions:", 1)[0]
        self.assertRegex(triggers, r"(?m)^  merge_group:$")
        # Pull requests never create queue branches; running there would only spend runners.
        self.assertNotRegex(triggers, r"(?m)^  (pull_request|push):")
        self.assertRegex(workflow, r"(?m)^permissions:\n  actions: write\n  contents: read$")
        self.assertIn("GH_TOKEN: ${{ github.token }}", workflow)
        self.assertIn("python3 scripts/python/merge_queue_cleanup.py", workflow)

    def test_ci_runs_these_regressions(self):
        workflow = (Path(__file__).resolve().parents[2] / ".github/workflows/ci.yml").read_text()
        self.assertRegex(workflow, r"python3 -m unittest [^\n]*\btest_merge_queue_cleanup\b")


if __name__ == "__main__":
    unittest.main()
