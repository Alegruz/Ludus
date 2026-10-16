"""Regression coverage for complete-change routing and required CI results."""
from __future__ import annotations

import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import ci_scope


class DocumentationScopeTests(unittest.TestCase):
    def test_documentation_and_its_tooling_can_skip_native_validation(self):
        paths = ["docs/development/api-reference.md", "docs/assets/wiki.css", "README.md"]
        self.assertTrue(ci_scope.documentation_only(paths))
        self.assertTrue(ci_scope.documentation_only(sorted(ci_scope.DOCUMENTATION_FILES)))

    def test_mixed_unknown_build_engine_and_policy_paths_require_native_validation(self):
        for path in (
            "modules/foundation/base/include/ludus/foundation/base/types.h",
            "apps/smoke/main.cpp", "CMakeLists.txt", "conan.lock",
            "config/tool_versions.json", "scripts/python/engine.py",
            ".github/workflows/ci.yml", ".github/workflows/project-sdk.yml",
            ".github/actions/change-scope/action.yml", "scripts/python/ci_scope.py",
            "scripts/python/test_ci_scope.py", "AGENTS.md", "unknown.md",
            "docs/../modules/engine.cpp", "/docs/page.md", "docs-other/page.md",
        ):
            with self.subTest(path=path):
                self.assertFalse(ci_scope.documentation_only(["docs/index.md", path]))
        self.assertFalse(ci_scope.documentation_only([]))

    def test_manual_missing_malformed_and_unavailable_comparisons_require_native_validation(self):
        for event_name, event in (
            ("workflow_dispatch", {}), ("schedule", {}), ("pull_request", {}), ("push", {}),
            ("push", {"before": "0" * 40, "after": "a" * 40}),
            ("push", {"before": "--help", "after": "a" * 40}),
            ("pull_request", {"pull_request": None}),
        ):
            with self.subTest(event=event):
                self.assertTrue(ci_scope.classify(event_name, event, Path.cwd())[0])
        with mock.patch("ci_scope.changed_paths", side_effect=subprocess.CalledProcessError(128, "git")):
            self.assertTrue(ci_scope.classify("push", {}, Path.cwd())[0])
        for event in ({}, {"merge_group": None}, {"merge_group": {"base_sha": "a" * 40}}):
            with self.subTest(event=event):
                self.assertTrue(ci_scope.classify("merge_group", event, Path.cwd())[0])


class ValidationTierTests(unittest.TestCase):
    @staticmethod
    def pull_request(*labels):
        return {"pull_request": {"labels": [{"name": label} for label in labels]}}

    def test_only_unlabeled_pull_requests_defer_merge_queue_jobs(self):
        self.assertEqual(ci_scope.validation_tier("pull_request", self.pull_request()), ci_scope.PULL_REQUEST_TIER)
        self.assertEqual(ci_scope.validation_tier("pull_request", self.pull_request("editor")),
                         ci_scope.PULL_REQUEST_TIER)
        self.assertEqual(ci_scope.validation_tier("pull_request", self.pull_request("editor", "ci:full")),
                         ci_scope.FULL_TIER)

    def test_queue_main_and_unknown_events_run_the_full_tier(self):
        for event_name in ("merge_group", "push", ""):
            with self.subTest(event_name=event_name):
                self.assertEqual(ci_scope.validation_tier(event_name, {}), ci_scope.FULL_TIER)

    def test_scheduled_and_manual_runs_add_the_nightly_advisory_jobs(self):
        for event_name in ("schedule", "workflow_dispatch"):
            with self.subTest(event_name=event_name):
                self.assertEqual(ci_scope.validation_tier(event_name, {}), ci_scope.NIGHTLY_TIER)

    def test_malformed_pull_request_metadata_runs_the_full_tier(self):
        for event in ({}, {"pull_request": None}, {"pull_request": {"labels": [{}]}},
                      {"pull_request": {"labels": ["ci:full"]}}):
            with self.subTest(event=event):
                self.assertEqual(ci_scope.validation_tier("pull_request", event), ci_scope.FULL_TIER)


class GitComparisonTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.git("init", "-b", "main")
        self.git("config", "user.name", "CI scope test")
        self.git("config", "user.email", "ci@example.invalid")
        self.write("docs/index.md", "Original docs")
        self.write("modules/engine.cpp", "Original engine")
        self.base = self.commit()

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.root, check=True, capture_output=True, text=True).stdout.strip()

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def commit(self):
        self.git("add", "--all")
        self.git("commit", "-m", "test change")
        return self.git("rev-parse", "HEAD")

    def pull_request(self, head, base=None):
        return {"pull_request": {"base": {"sha": base or self.base}, "head": {"sha": head}}}

    def test_documentation_push_and_complete_pr_are_exempt(self):
        self.write("docs/index.md", "Updated docs")
        head = self.commit()
        self.assertFalse(ci_scope.classify("push", {"before": self.base, "after": head}, self.root)[0])
        self.assertFalse(ci_scope.classify("pull_request", self.pull_request(head), self.root)[0])

    def test_latest_docs_commit_does_not_hide_earlier_engine_changes(self):
        self.write("modules/engine.cpp", "Changed engine")
        engine_head = self.commit()
        self.write("docs/index.md", "Updated docs")
        head = self.commit()
        self.assertFalse(ci_scope.classify("push", {"before": engine_head, "after": head}, self.root)[0])
        self.assertTrue(ci_scope.classify("pull_request", self.pull_request(head), self.root)[0])

    def test_pr_uses_merge_base_when_target_branch_advances(self):
        self.git("checkout", "-b", "docs-change")
        self.write("docs/index.md", "Updated docs")
        head = self.commit()
        self.git("checkout", "main")
        self.write("modules/engine.cpp", "Unrelated upstream change")
        base = self.commit()
        self.assertFalse(ci_scope.classify("pull_request", self.pull_request(head, base), self.root)[0])

    def test_merge_group_compares_the_queued_candidate_with_its_base(self):
        self.write("docs/index.md", "Updated docs")
        docs_head = self.commit()
        group = {"merge_group": {"base_sha": self.base, "head_sha": docs_head}}
        self.assertFalse(ci_scope.classify("merge_group", group, self.root)[0])
        self.write("modules/engine.cpp", "Queued engine change")
        engine_head = self.commit()
        group = {"merge_group": {"base_sha": self.base, "head_sha": engine_head}}
        self.assertTrue(ci_scope.classify("merge_group", group, self.root)[0])

    def test_engine_rename_into_documentation_is_not_exempt(self):
        self.git("mv", "modules/engine.cpp", "docs/engine.cpp")
        head = self.commit()
        paths = ci_scope.changed_paths("pull_request", self.pull_request(head), self.root)
        self.assertIn("modules/engine.cpp", paths)
        self.assertTrue(ci_scope.classify("pull_request", self.pull_request(head), self.root)[0])

    def test_documentation_deletions_and_unusual_names_remain_exempt(self):
        self.git("rm", "docs/index.md")
        self.write("docs/space and\nnewline.md", "New documentation")
        head = self.commit()
        paths = ci_scope.changed_paths("pull_request", self.pull_request(head), self.root)
        self.assertIn("docs/space and\nnewline.md", paths)
        self.assertIn("docs/index.md", paths)
        self.assertFalse(ci_scope.classify("pull_request", self.pull_request(head), self.root)[0])

    def test_complete_file_list_includes_engine_change_after_300_docs_files(self):
        for index in range(305):
            self.write(f"docs/page-{index:03}.md", "New docs")
        self.write("modules/engine.cpp", "Changed engine")
        head = self.commit()
        self.assertTrue(ci_scope.classify("pull_request", self.pull_request(head), self.root)[0])

    def test_empty_diff_and_missing_history_require_native_validation(self):
        self.assertTrue(ci_scope.classify("push", {"before": self.base, "after": self.base}, self.root)[0])
        self.assertTrue(ci_scope.classify("push", {"before": "a" * 40, "after": self.base}, self.root)[0])

    def test_cli_outputs_github_routing_and_summary(self):
        self.write("docs/index.md", "Updated docs")
        head = self.commit()
        event = self.root / "event.json"
        output, summary = self.root / "output", self.root / "summary"
        event.write_text(json.dumps(self.pull_request(head)), encoding="utf-8")
        subprocess.run(
            [sys.executable, ci_scope.__file__, "classify"], cwd=self.root, check=True, capture_output=True,
            env={**os.environ, "GITHUB_EVENT_NAME": "pull_request", "GITHUB_EVENT_PATH": str(event),
                 "GITHUB_OUTPUT": str(output), "GITHUB_STEP_SUMMARY": str(summary)},
        )
        self.assertEqual(output.read_text(), "native_required=false\nvalidation_tier=pull-request\n")
        self.assertIn("Native validation required: false. Validation tier: pull-request.", summary.read_text())


class RequiredGateTests(unittest.TestCase):
    def test_workflow_gate_requires_every_classified_native_job(self):
        workflow = (Path(__file__).resolve().parents[2] / ".github/workflows/ci.yml").read_text()
        gate = workflow.split("\n  linux:", 1)[1]
        dependencies = re.search(r"needs: \[([^\]]+)\]", gate)
        self.assertIsNotNone(dependencies)
        self.assertCountEqual([name.strip() for name in dependencies.group(1).split(",")],
                              ["changes", *ci_scope.NATIVE_JOBS])
        # A missing workflow job must not silently remove validation while the
        # Python gate still expects it, including docs-only routing.
        jobs = set(re.findall(r"^  ([a-z][a-z0-9-]*):$", workflow, re.MULTILINE))
        self.assertTrue(set(ci_scope.NATIVE_JOBS) <= jobs)

    def test_workflow_defers_exactly_the_merge_queue_and_nightly_jobs(self):
        workflow = (Path(__file__).resolve().parents[2] / ".github/workflows/ci.yml").read_text()
        self.assertTrue(set(ci_scope.MERGE_QUEUE_JOBS) <= set(ci_scope.NATIVE_JOBS))
        self.assertTrue(set(ci_scope.NIGHTLY_JOBS) <= set(ci_scope.NATIVE_JOBS))
        self.assertFalse(set(ci_scope.MERGE_QUEUE_JOBS) & set(ci_scope.NIGHTLY_JOBS))
        conditions = dict(re.findall(r"^  ([a-z][a-z0-9-]*):\n(?:    .*\n)*?    if: (.*)$", workflow, re.MULTILINE))
        for name in ci_scope.NATIVE_JOBS:
            with self.subTest(name=name):
                condition = conditions.get(name, "")
                self.assertEqual("needs.changes.outputs.validation_tier != 'pull-request'" in condition,
                                 name in ci_scope.MERGE_QUEUE_JOBS)
                self.assertEqual("needs.changes.outputs.validation_tier == 'nightly'" in condition,
                                 name in ci_scope.NIGHTLY_JOBS)
        # A merge-queue job keyed to the full tier alone would vanish from nightly runs.
        self.assertNotIn("validation_tier == 'full'", workflow)

    def test_advisory_analyzers_run_nightly_and_required_inventory_runs_in_the_queue(self):
        root = Path(__file__).resolve().parents[2] / ".github/workflows"
        nightly = {
            "ci.yml": "if: needs.changes.outputs.validation_tier == 'nightly'",
            "webgpu-probe.yml": "if: github.event_name != 'pull_request'",
        }
        for name, condition in nightly.items():
            jobs = re.findall(r"(?ms)^  ([a-z][a-z0-9-]*):\n(.*?)(?=^  [a-z][a-z0-9-]*:\n|\Z)",
                              root.joinpath(name).read_text().split("\njobs:\n", 1)[1])
            analyzer_steps = 0
            for job, body in jobs:
                header = body.split("\n    steps:", 1)[0]
                for step in re.findall(r"(?ms)^      - name: [^\n]*\n((?:        [^\n]*\n)+)", body):
                    if "--analyzer" not in step:
                        continue
                    analyzer_steps += 1
                    with self.subTest(workflow=name, job=job):
                        # The step itself or its whole job must be nightly-only.
                        self.assertTrue(condition in step or condition.replace("if: ", "") in header)
                        self.assertIn("continue-on-error: true", step)
            with self.subTest(workflow=name):
                self.assertTrue(analyzer_steps)
        self.assertRegex(root.joinpath("webgpu-probe.yml").read_text(), r"(?m)^  schedule:\n    - cron: '[^']+'$")
        workflow = root.joinpath("ci.yml").read_text()
        macos_compile = workflow.split("\n  macos-compile:\n", 1)[1].split("\n  macos-sanitizers:\n", 1)[0]
        inventory = macos_compile.split("--inventory", 1)
        self.assertEqual(len(inventory), 2)
        self.assertNotIn("continue-on-error", inventory[0].rsplit("- name:", 1)[1])
        self.assertNotIn("--inventory", workflow.split("\n  macos-analysis:\n", 1)[1].split("\n  formatting:\n", 1)[0])

    def test_builds_and_tidy_use_the_runner_core_counts(self):
        # Hosted runner CPUs for public repositories; a runner label not listed
        # here must be added with its core count before a job can use it.
        cores = {"ubuntu-24.04": "4", "macos-15": "3"}
        root = Path(__file__).resolve().parents[2] / ".github/workflows"
        workflow = root.joinpath("ci.yml").read_text()
        self.assertIn('\n  CMAKE_BUILD_PARALLEL_LEVEL: "4"\n', workflow.split("\njobs:\n", 1)[0])
        jobs = dict(re.findall(r"(?ms)^  ([a-z][a-z0-9-]*):\n(.*?)^    steps:", workflow.split("\njobs:\n", 1)[1]))
        for name, header in jobs.items():
            runner = re.search(r"(?m)^    runs-on: (\S+)$", header)
            if runner is None:
                continue
            level = re.search(r'(?m)^      CMAKE_BUILD_PARALLEL_LEVEL: "(\d+)"$', header)
            tidy = re.search(r'(?m)^      LUDUS_TIDY_JOBS: "(\d+)"$', header)
            with self.subTest(job=name, runner=runner.group(1)):
                self.assertIn(runner.group(1), cores)
                expected = cores[runner.group(1)]
                if name == "build-budget":
                    self.assertEqual(level.group(1), "2")
                elif expected == "4":
                    # The workflow default already matches Linux runners.
                    self.assertIsNone(level)
                else:
                    self.assertEqual(level.group(1) if level else None, expected)
                if tidy:
                    self.assertEqual(tidy.group(1), expected)
        for name in ("ci.yml", "webgpu-probe.yml"):
            with self.subTest(workflow=name):
                self.assertNotRegex(root.joinpath(name).read_text(), r"--parallel \d")

    def test_required_workflows_report_checks_to_the_merge_queue(self):
        # A required check that never runs for merge_group stalls every queued PR.
        for name in ("ci.yml", "project-sdk.yml", "wiki.yml"):
            with self.subTest(workflow=name):
                workflow = (Path(__file__).resolve().parents[2] / ".github/workflows" / name).read_text()
                self.assertRegex(workflow, r"(?m)^  merge_group:")

    def test_main_reruns_full_ci_only_for_cache_inputs_and_nightly(self):
        # The queue validates every landed commit; main runs exist to refresh
        # the caches pull requests restore, so they must cover cache-key inputs.
        workflow = (Path(__file__).resolve().parents[2] / ".github/workflows/ci.yml").read_text()
        triggers = workflow.split("\npermissions:", 1)[0]
        push = triggers.split("  push:", 1)[1].split("\n  schedule:", 1)[0]
        for path in ("conanfile.py", "conan.lock", "config/tool_versions.json", "CMakePresets.json",
                     ".github/actions/**"):
            with self.subTest(path=path):
                self.assertIn(f"'{path}'", push)
        self.assertRegex(triggers, r"(?m)^  schedule:\n    - cron: '[^']+'$")

    def test_pull_requests_leave_macos_tooling_jobs_to_the_queue(self):
        root = Path(__file__).resolve().parents[2] / ".github/workflows"
        wiki = root.joinpath("wiki.yml").read_text().split("\n  macos-documentation-tools:\n", 1)[1]
        self.assertIn("    if: github.event_name != 'pull_request'\n", wiki.split("\n    steps:", 1)[0])
        butler = root.joinpath("project-sdk.yml").read_text().split("\n  butler-install:\n", 1)[1]
        runners = re.search(r"runner: \$\{\{ fromJSON\(github.event_name == 'pull_request' && '(\[.*?\])' \|\| '(\[.*?\])'\) \}\}",
                            butler)
        self.assertIsNotNone(runners)
        self.assertEqual(json.loads(runners.group(1)), ["ubuntu-24.04"])
        self.assertEqual(json.loads(runners.group(2)),
                         ["ubuntu-24.04", "macos-15", "macos-15-intel"])

    def results(self, native_required=True, tier=ci_scope.FULL_TIER):
        def expected(name):
            return "success" if native_required and not self.deferred(name, tier) else "skipped"
        return {
            "changes": {"result": "success", "outputs": {
                "native_required": "true" if native_required else "false", "validation_tier": tier}},
            **{name: {"result": expected(name)} for name in ci_scope.NATIVE_JOBS},
        }

    @staticmethod
    def deferred(name, tier):
        return ((tier == ci_scope.PULL_REQUEST_TIER and name in ci_scope.MERGE_QUEUE_JOBS)
                or (tier != ci_scope.NIGHTLY_TIER and name in ci_scope.NIGHTLY_JOBS))

    def test_all_native_successes_or_intentional_docs_skips_pass(self):
        for tier in sorted(ci_scope.TIERS):
            with self.subTest(tier=tier):
                self.assertEqual(ci_scope.gate_failures(self.results(tier=tier)), [])
                self.assertEqual(ci_scope.gate_failures(self.results(False, tier)), [])

    def test_each_tier_requires_its_jobs_and_skips_only_deferred_jobs(self):
        # Full runs skip only nightly advisory jobs, pull requests also skip the
        # merge-queue jobs, and nightly runs require every native job.
        for tier in sorted(ci_scope.TIERS):
            for name in ci_scope.NATIVE_JOBS:
                deferred = self.deferred(name, tier)
                statuses = ("failure", "cancelled", "success") if deferred else ("failure", "cancelled", "skipped")
                for status in statuses:
                    with self.subTest(tier=tier, name=name, status=status):
                        results = self.results(tier=tier)
                        results[name]["result"] = status
                        self.assertTrue(ci_scope.gate_failures(results))
        self.assertTrue(all(not self.deferred(name, ci_scope.NIGHTLY_TIER) for name in ci_scope.NATIVE_JOBS))

    def test_missing_or_invalid_tier_cannot_skip_merge_queue_jobs(self):
        for tier in (None, "", "Full", "fast"):
            with self.subTest(tier=tier):
                results = self.results(tier=ci_scope.PULL_REQUEST_TIER)
                results["changes"]["outputs"]["validation_tier"] = tier
                self.assertTrue(ci_scope.gate_failures(results))

    def test_docs_exemption_does_not_hide_failed_cancelled_or_executed_jobs(self):
        for status in ("failure", "cancelled", "success"):
            results = self.results(False)
            results["formatting"]["result"] = status
            self.assertTrue(ci_scope.gate_failures(results))

    def test_failed_or_missing_classifier_cannot_bypass_required_check(self):
        for status in ("failure", "cancelled", "skipped", None):
            results = self.results(False)
            results["changes"]["result"] = status
            self.assertTrue(ci_scope.gate_failures(results))
        results = self.results(False)
        del results["changes"]
        self.assertTrue(ci_scope.gate_failures(results))
        for output in (None, "", "False", "invalid"):
            results = self.results(False)
            results["changes"]["outputs"]["native_required"] = output
            self.assertTrue(ci_scope.gate_failures(results))

    def test_missing_or_unexpected_native_jobs_fail(self):
        results = self.results()
        del results["sanitizers"]
        self.assertTrue(ci_scope.gate_failures(results))
        results = self.results(False)
        results["unknown"] = {"result": "skipped"}
        self.assertTrue(ci_scope.gate_failures(results))


if __name__ == "__main__":
    unittest.main()
