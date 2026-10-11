"""Negative controls for omitted analysis, incomplete output and false sanitizer passes."""
from __future__ import annotations

import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import analysis_inventory as inventory
import engine
from quality_process import group_is_live, LogBudget, ProcessResult, run_bounded
import sanitizer_control


class TemporaryTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name).resolve()


class ProcessTests(TemporaryTest):
    def test_zombie_group_is_distinguished_from_live_or_unreadable_cleanup(self):
        self.assertFalse(group_is_live(42, "42 Z\n99 S\n"))
        self.assertTrue(group_is_live(42, "42 Z\n42 S\n"))
        with self.assertRaises(OSError):
            group_is_live(42, "unknown\n")

    def test_failed_process_preserves_original_output_and_exit(self):
        result = run_bounded([sys.executable, "-c", "import os; os.write(2, b'diagnostic\\xff'); exit(7)"],
                             cwd=self.root, log=self.root / "failure.log")
        self.assertEqual(result.returncode, 7)
        self.assertEqual(result.log.read_bytes(), b"diagnostic\xff")
        self.assertTrue(result.complete)

    def test_output_limit_terminates_a_noisy_process_and_invalidates_evidence(self):
        result = run_bounded([sys.executable, "-c", "import os\nwhile True: os.write(1,b'x'*8192)"],
                             cwd=self.root, log=self.root / "noisy.log", limit=1024, timeout=3)
        self.assertTrue(result.truncated)
        self.assertFalse(result.complete)
        self.assertEqual(result.log.stat().st_size, 1024)

    def test_timeout_kills_a_descendant_holding_the_output_pipe(self):
        script = "import subprocess,sys; subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)'])"
        result = run_bounded([sys.executable, "-c", script], cwd=self.root,
                             log=self.root / "timeout.log", timeout=0.2)
        self.assertTrue(result.timed_out)
        self.assertFalse(result.complete)

    def test_timeout_applies_after_a_child_closes_its_output_pipe(self):
        script = "import os,time; os.close(1); os.close(2); time.sleep(30)"
        result = run_bounded([sys.executable, "-c", script], cwd=self.root,
                             log=self.root / "closed-pipe.log", timeout=0.2)
        self.assertTrue(result.timed_out)
        self.assertFalse(result.complete)

    def test_shared_log_budget_bounds_the_whole_lane(self):
        budget = LogBudget(20)
        results = [run_bounded([sys.executable, "-c", "print('0123456789')"], cwd=self.root,
                               log=self.root / f"{index}.log", budget=budget) for index in range(2)]
        self.assertEqual(sum(result.bytes_written for result in results), 20)
        self.assertTrue(results[0].complete)
        self.assertTrue(results[1].truncated)

    def test_empty_analysis_fails_without_sharding(self):
        with patch.dict(os.environ, {}, clear=True), self.assertRaisesRegex(engine.EngineError, "no translation units"):
            engine.run_analysis_commands(self.root, [])

    def test_missing_required_log_fails_even_when_process_exits_zero(self):
        missing = ProcessResult(0, self.root / "missing.log", 0, False, False)
        with patch.object(engine, "run_bounded", return_value=missing), \
                self.assertRaisesRegex(engine.EngineError, "1 translation unit"), \
                contextlib.redirect_stdout(io.StringIO()):
            engine.run_analysis_commands(self.root, [["tidy", "input.cpp"]])

    def test_overflow_preserves_the_failure_report(self):
        with patch.object(engine, "LOG_BYTES", 8), contextlib.redirect_stdout(io.StringIO()), \
                self.assertRaises(engine.EngineError):
            engine.run_analysis_commands(self.root, [[sys.executable, "-c", "print('very long diagnostic')"]])
        report = json.loads(next(self.root.glob("out/quality/analysis/run-*/results.json")).read_text())
        self.assertEqual(report["results"][0]["state"], "fail")
        self.assertTrue(report["results"][0]["truncated"])


class InventoryTests(TemporaryTest):
    def fixture(self, names):
        self.build = self.root / "out/build/test"
        reply = self.build / ".cmake/api/v1/reply"
        reply.mkdir(parents=True)
        (reply / "index-1.json").write_text(json.dumps({"reply": {"client-ludus-quality": {"codemodel-v2": {"jsonFile": "model.json"}}}}))
        (reply / "model.json").write_text(json.dumps({"version": {"major": 2}, "paths": {"source": str(self.root), "build": str(self.build)},
            "configurations": [{"targets": [{"jsonFile": "target.json"}]}]}))
        (reply / "target.json").write_text(json.dumps({"compileGroups": [{"language": "CXX"}], "sources": [
            {"path": name, "compileGroupIndex": 0} for name in names]}))
        self.entries = []
        for index, name in enumerate(names):
            source = self.root / name
            source.parent.mkdir(parents=True, exist_ok=True)
            source.touch()
            self.entries.append({"directory": str(self.root), "file": name,
                                 "arguments": ["clang++", f"-DVARIANT={index}", "-c", name]})
        self.write_database()

    def write_database(self):
        (self.build / "compile_commands.json").write_text(json.dumps(self.entries))

    def test_all_first_party_roots_languages_and_variants_are_accounted_for(self):
        names = ["modules/a.c", "apps/b.mm", "tools/c.m", "tests/d.cc", "examples/e.cxx", "modules/a.c"]
        self.fixture(names)
        # An exact duplicate is redundant; distinct defines remain separate.
        self.entries.append(self.entries[0])
        self.entries[1]["file"] = str(self.root / names[1])
        self.write_database()
        selected, report = inventory.inventory(self.root, self.build)
        self.assertEqual(len(selected), len(names))
        self.assertEqual(report["expected_commands"], len(names))
        self.assertEqual(len(report["sources"]), 5)
        self.assertEqual(len([entry for entry in selected if entry["file"].endswith("a.c")]), 2)

    def test_missing_source_or_compile_variant_is_an_error(self):
        for names in (["modules/a.cpp", "tests/b.cpp"], ["modules/a.cpp", "modules/a.cpp"]):
            with self.subTest(names=names):
                # Each fixture needs its own independent CMake reply directory.
                if (self.root / "out").exists():
                    shutil.rmtree(self.root / "out")
                self.fixture(names)
                self.entries.pop()
                self.write_database()
                with self.assertRaisesRegex(ValueError, "omissions"):
                    inventory.inventory(self.root, self.build)

    def test_empty_and_malformed_database_and_missing_inventory_fail(self):
        self.fixture(["modules/a.cpp"])
        for value in ([], {}, [{"file": "modules/a.cpp"}],
                      [{"directory": "relative", "file": "x.cpp", "command": "clang++ x.cpp"}]):
            (self.build / "compile_commands.json").write_text(json.dumps(value))
            with self.subTest(value=value), self.assertRaises(ValueError):
                inventory.inventory(self.root, self.build)
        shutil.rmtree(self.build / ".cmake")
        with self.assertRaisesRegex(ValueError, "no CMake"):
            inventory.inventory(self.root, self.build)

    def test_response_files_retain_the_compile_directory_and_missing_files_fail(self):
        self.fixture(["modules/a.cpp"])
        response = self.root / "defines.rsp"
        response.write_text("-DSELECTED_VARIANT=1\n")
        self.entries[0]["arguments"].insert(1, "@defines.rsp")
        self.write_database()
        selected, _ = inventory.inventory(self.root, self.build)
        self.assertIn("@" + str(response), selected[0]["arguments"])
        response.unlink()
        with self.assertRaisesRegex(ValueError, "build analysis inputs first"):
            inventory.inventory(self.root, self.build)

    def test_unbuilt_generated_and_external_sources_are_explicitly_excluded(self):
        self.fixture(["modules/a.cpp"])
        reply = self.build / ".cmake/api/v1/reply/target.json"
        target = json.loads(reply.read_text())
        target["sources"].append({"path": "out/build/test/moc.cpp", "compileGroupIndex": 0, "isGenerated": True})
        reply.write_text(json.dumps(target))
        for name in ("out/build/test/moc.cpp", "third_party/library.cpp"):
            self.entries.append({"directory": str(self.root), "file": name, "command": f"clang++ -c {name}"})
        self.write_database()
        selected, report = inventory.inventory(self.root, self.build)
        self.assertEqual(len(selected), 1)
        self.assertEqual(len(report["excluded"]), 2)
        self.assertTrue(all(entry["reason"] for entry in report["excluded"]))

    @unittest.skipUnless(shutil.which("cmake"), "CMake is needed for the independent inventory integration test")
    def test_real_cmake_inventory_detects_a_deleted_command(self):
        self.build = self.root / "out/build/test"
        source = self.root / "tools/sample.cpp"
        source.parent.mkdir()
        source.write_text("int value() { return 3; }\n")
        (self.root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(Inventory LANGUAGES CXX)\n'
            'set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
            'add_library(one OBJECT tools/sample.cpp)\n'
            'add_library(two OBJECT tools/sample.cpp)\ntarget_compile_definitions(two PRIVATE SECOND=1)\n')
        inventory.request_codemodel(self.build)
        subprocess.run(["cmake", "-S", self.root, "-B", self.build], check=True, capture_output=True)
        selected, report = inventory.inventory(self.root, self.build)
        self.assertEqual(len(selected), 2)
        self.assertEqual(report["expected_commands"], 2)
        self.entries = selected[:1]
        self.write_database()
        with self.assertRaisesRegex(ValueError, "expected 2, found 1"):
            inventory.inventory(self.root, self.build)


class SanitizerDriverTests(TemporaryTest):
    def child(self, body):
        path = self.root / "child"
        path.write_text(f"#!{sys.executable}\nimport sys\n" + body)
        path.chmod(0o755)
        return path

    def verify(self, body):
        with contextlib.redirect_stdout(io.StringIO()):
            return sanitizer_control.verify("ubsan", self.child(body), self.root / "evidence", "none")

    def test_recoverable_finding_is_a_failure(self):
        self.assertFalse(self.verify("if len(sys.argv)>1: print('runtime error: signed integer overflow')\n"))

    def test_unrelated_crash_and_runtime_startup_failure_are_failures(self):
        self.assertFalse(self.verify("sys.exit(9 if len(sys.argv)>1 else 0)\n"))
        self.assertFalse(self.verify("print('runtime error: signed integer overflow'); sys.exit(1)\n"))

    def test_specific_nonzero_finding_and_clean_startup_are_required(self):
        self.assertTrue(self.verify("if len(sys.argv)>1:\n print('runtime error: signed integer overflow'); sys.exit(1)\n"))
        self.assertEqual(len(list((self.root / "evidence").glob("run-*/results.json"))), 1)


if __name__ == "__main__":
    unittest.main()
