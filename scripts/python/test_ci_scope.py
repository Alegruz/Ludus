"""Regression coverage for complete-change routing and required CI results."""
from __future__ import annotations

import json
import os
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
            ("workflow_dispatch", {}), ("pull_request", {}), ("push", {}),
            ("push", {"before": "0" * 40, "after": "a" * 40}),
            ("push", {"before": "--help", "after": "a" * 40}),
            ("pull_request", {"pull_request": None}),
        ):
            with self.subTest(event=event):
                self.assertTrue(ci_scope.classify(event_name, event, Path.cwd())[0])
        with mock.patch("ci_scope.changed_paths", side_effect=subprocess.CalledProcessError(128, "git")):
            self.assertTrue(ci_scope.classify("push", {}, Path.cwd())[0])


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
        self.assertEqual(output.read_text(), "native_required=false\n")
        self.assertIn("Native validation required: false", summary.read_text())


class RequiredGateTests(unittest.TestCase):
    def results(self, native_required=True):
        return {
            "changes": {"result": "success", "outputs": {"native_required": "true" if native_required else "false"}},
            **{name: {"result": "success" if native_required else "skipped"} for name in ci_scope.NATIVE_JOBS},
        }

    def test_all_native_successes_or_intentional_docs_skips_pass(self):
        self.assertEqual(ci_scope.gate_failures(self.results()), [])
        self.assertEqual(ci_scope.gate_failures(self.results(False)), [])

    def test_failed_cancelled_or_skipped_native_jobs_fail_full_validation(self):
        for name in ci_scope.NATIVE_JOBS:
            for status in ("failure", "cancelled", "skipped"):
                with self.subTest(name=name, status=status):
                    results = self.results()
                    results[name]["result"] = status
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
