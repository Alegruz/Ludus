"""Exercise malformed inputs and real checker diagnostics, not copied rule logic."""
from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import engine
import tooling_quality as quality

LLVM_MAJOR = engine.pinned_llvm_major(engine.repo_root())


class QualityToolsTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def test_tracked_extensionless_python_and_nested_scripts_are_inventoried(self):
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        for name, contents in (("scripts/check-docs", "#!/usr/bin/env python3\n"),
                               ("tools/a.py", "pass\n"), ("third_party/vendor.py", "pass\n"),
                               ("scripts/check", "#!/usr/bin/env bash\n")):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents)
        subprocess.run(["git", "add", "."], cwd=self.root, check=True)
        self.assertEqual(["scripts/check-docs", "tools/a.py"],
                         [str(p.relative_to(self.root)) for p in quality.python_sources(self.root)])

    def test_archive_checksum_mismatch_cannot_replace_existing_executable(self):
        executable = self.root / "out/host-tools/bin/actionlint"
        executable.parent.mkdir(parents=True)
        executable.write_text("existing")
        with self.assertRaisesRegex(engine.EngineError, "checksum"):
            quality.install_actionlint(self.root, b"corrupt", "0" * 64)
        self.assertEqual("existing", executable.read_text())

    def test_empty_tracked_python_scope_fails(self):
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        with self.assertRaisesRegex(engine.EngineError, "scope is empty"):
            quality.python_sources(self.root)

    def test_wrong_tool_version_preserves_error_and_does_not_run_checks(self):
        (self.root / ".github/workflows").mkdir(parents=True)
        (self.root / ".github/workflows/a.yml").write_text("name: a\n")
        executable = engine.managed_executable(self.root, "ruff")
        executable.parent.mkdir(parents=True)
        executable.write_text("#!/bin/sh\necho ruff 0.0.1\n")
        executable.chmod(0o755)
        with patch.object(engine, "load_tool_versions", return_value={"quality": {"ruff": "0.17.0", "actionlint": {"version": "1.7.11"}}}), \
                patch.object(quality, "python_sources", return_value=[self.root / "a.py"]), \
                self.assertRaisesRegex(engine.EngineError, "pinned version"):
            quality.check(self.root)
        report = json.loads(next((self.root / "out/quality/tooling").glob("run-*/results.json")).read_text())
        self.assertEqual("error", report["state"])
        self.assertEqual([], report["results"])

    def test_archive_link_is_rejected_even_with_correct_checksum(self):
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode="w:gz") as package:
            entry = tarfile.TarInfo("actionlint")
            entry.type = tarfile.SYMTYPE
            entry.linkname = "../../outside"
            package.addfile(entry)
        data = stream.getvalue()
        with self.assertRaisesRegex(engine.EngineError, "Invalid"):
            quality.install_actionlint(self.root, data, hashlib.sha256(data).hexdigest())

    def test_missing_tool_preserves_error_report(self):
        (self.root / ".github/workflows").mkdir(parents=True)
        (self.root / ".github/workflows/a.yml").write_text("name: a\n")
        with patch.object(engine, "load_tool_versions", return_value={"quality": {"ruff": "0.17.0", "actionlint": {"version": "1.7.11"}}}), \
                patch.object(quality, "python_sources", return_value=[self.root / "a.py"]), \
                self.assertRaisesRegex(engine.EngineError, "Missing ruff"):
            quality.check(self.root)
        report = json.loads(next((self.root / "out/quality/tooling").glob("run-*/results.json")).read_text())
        self.assertEqual("error", report["state"])
        self.assertIn("Missing ruff", report["error"])

    def test_real_ruff_rejects_undefined_name_in_extensionless_entry_point(self):
        executable = engine.managed_executable(engine.repo_root(), "ruff")
        if not executable.is_file():
            self.skipTest("Prepare pinned Ruff with scripts/setup-quality-tools")
        path = self.root / "entry"
        path.write_text("#!/usr/bin/env python3\nprint(undefined_quality_control)\n")
        result = subprocess.run([str(executable), "check", "--config", str(engine.repo_root() / "ruff.toml"), str(path)], text=True, capture_output=True)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("F821", result.stdout)

    def test_real_actionlint_rejects_invalid_event(self):
        executable = engine.host_tools_bin_dir(engine.repo_root()) / "actionlint"
        if not executable.is_file():
            self.skipTest("Prepare pinned actionlint with scripts/setup-quality-tools")
        path = self.root / "invalid.yml"
        path.write_text("on: invalid_quality_event\njobs:\n  check:\n    runs-on: ubuntu-24.04\n    steps:\n      - run: true\n")
        result = subprocess.run([str(executable), "-shellcheck=", "-pyflakes=", str(path)], text=True, capture_output=True)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("invalid_quality_event", result.stdout + result.stderr)

    def test_real_analyzer_rejects_known_null_dereference(self):
        executable = shutil.which(f"clang-tidy-{LLVM_MAJOR}") or shutil.which("clang-tidy")
        if not executable:
            self.skipTest("Reference clang-tidy unavailable")
        version = subprocess.run([executable, "--version"], capture_output=True, text=True, check=True)
        self.assertIn(f"version {LLVM_MAJOR}.", version.stdout)
        source = self.root / "fault.cpp"
        source.write_text("int Fault() { int* value = nullptr; return *value; }\n")
        result = subprocess.run([executable, f"--config-file={engine.repo_root() / 'config/clang-analyzer.yml'}",
            "--warnings-as-errors=*", str(source), "--", "-std=c++23", "-fno-exceptions"], text=True, capture_output=True)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("clang-analyzer-core.NullDereference", result.stdout + result.stderr)

    def test_real_analyzer_respects_owned_moved_state_contract(self):
        executable = shutil.which(f"clang-tidy-{LLVM_MAJOR}") or shutil.which("clang-tidy")
        if not executable:
            self.skipTest("Reference clang-tidy unavailable")
        source = self.root / "moved.cpp"
        source.write_text("struct Buffer { Buffer() {} Buffer(Buffer&&) noexcept {} int Read() const { return 1; } };\n"
                          "int Verify() { Buffer original; Buffer transferred(static_cast<Buffer&&>(original)); return original.Read(); }\n")
        result = subprocess.run([executable, f"--config-file={engine.repo_root() / 'config/clang-analyzer.yml'}",
            "--warnings-as-errors=*", str(source), "--", "-std=c++23", "-fno-exceptions"], text=True, capture_output=True)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
