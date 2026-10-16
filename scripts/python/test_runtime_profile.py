"""Runtime profiling runner: manifest contract, parsers, failure semantics and report-only comparison."""
import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import runtime_profile

ROOT = Path(__file__).resolve().parents[2]

# Captured from the Profile-flavor harnesses; trimmed but unedited lines.
THREADING_OUTPUT = """jobs=256 iterations=0 workers=0 median_ns=1833 checksum=256
jobs=256 iterations=0 workers=4 median_ns=27167 checksum=256
jobs=256 iterations=10000 workers=4 median_ns=803750 checksum=18367026793921049022
"""
STRINGS_OUTPUT = """String_bytes,48
SharedString_bytes,8
case,ns_per_operation
unique_construct_assign,26
xxh3_64,3
checksum,26371200
"""
INPUT_OUTPUT = """# Ludus keyboard reducer — performance evidence
allocations: construction=1, hot-loop(5000 iters)=0
idle step:              median=42.0 ns  p95=42.0 ns
256 transitions/256 bindings (map=Ok): median=20292.0 ns  p95=20791.0 ns
trace off: median=42.0 ns   trace on: median=42.0 ns
"""
CONTAINER_OUTPUT = """count,sorted_lookup_ns,hash_lookup_ns,tree_lookup_ns,sorted_insert_ns,hash_insert_ns,sorted_storage_bytes
1,4.86,7.35,5.82,3.25,37.83,16
16,9.97,6.03,15.08,11.81,20.18,256
"""


def trace_document(tick_microseconds=1.0):
    events = []
    for frame in range(3):
        start = frame * 10.0
        events += [
            {"pid": 1, "tid": 0, "ts": start, "ph": "B", "name": "Tick"},
            {"pid": 1, "tid": 0, "ts": start + 1, "ph": "B", "name": "Motion"},
            {"pid": 1, "tid": 0, "ts": start + 1 + tick_microseconds, "ph": "E", "name": "Motion"},
            {"pid": 1, "tid": 0, "ts": start + 4, "ph": "E", "name": "Tick"},
            {"pid": 1, "tid": 0, "ts": start + 4, "ph": "i", "name": "FrameMark", "s": "t"},
        ]
    return {"traceEvents": events, "displayTimeUnit": "ns"}


def scenario_entry(**overrides):
    entry = {
        "id": "sample", "owner": "modules/sample", "description": "Sample scenario.", "target": "ludus_sample",
        "executable": "bin/sample", "arguments": [], "cmake_options": {}, "requires": [], "trace": False,
        "parser": {"kind": "key-value", "identity": ["workers"], "metrics": ["median_ns"]},
        "warmup": 0, "repetitions": 3, "timeout_seconds": 10,
    }
    entry.update(overrides)
    return entry


def write_manifest(directory, *entries):
    path = Path(directory) / "manifest.json"
    path.write_text(json.dumps({"schema": 1, "scenarios": list(entries)}))
    return path


class Manifest(unittest.TestCase):
    def test_repository_manifest_is_valid_and_builds_declared_executables(self):
        scenarios = runtime_profile.load_manifest(ROOT / runtime_profile.MANIFEST_PATH)
        self.assertTrue(scenarios)
        hosted = [scenario for scenario in scenarios if not set(scenario.requires) - {"shader-tools"}]
        self.assertTrue(any(scenario.trace for scenario in hosted), "hosted lanes need a trace-producing scenario")
        listed = subprocess.run(["git", "ls-files", "*CMakeLists.txt"], cwd=ROOT, stdout=subprocess.PIPE, text=True,
                                check=True).stdout.split()
        cmake_text = "\n".join((ROOT / path).read_text() for path in listed)
        for scenario in scenarios:
            with self.subTest(scenario=scenario.id):
                self.assertIn(f"add_executable({scenario.target}", cmake_text)

    def test_rejects_unknown_or_missing_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            entry = scenario_entry(extra=True)
            with self.assertRaisesRegex(runtime_profile.ProfileError, "unknown \\['extra'\\]"):
                runtime_profile.load_manifest(write_manifest(directory, entry))
            entry = scenario_entry()
            del entry["timeout_seconds"]
            with self.assertRaisesRegex(runtime_profile.ProfileError, "missing \\['timeout_seconds'\\]"):
                runtime_profile.load_manifest(write_manifest(directory, entry))

    def test_rejects_invalid_values(self):
        cases = {
            "id": scenario_entry(id="Bad_ID"),
            "relative": scenario_entry(executable="../escape"),
            "arguments": scenario_entry(arguments=""),
            "option": scenario_entry(cmake_options={"CMAKE_BUILD_TYPE": "Debug"}),
            "repetitions": scenario_entry(repetitions=0),
            "parser": scenario_entry(parser={"kind": "unknown"}),
            "trace": scenario_entry(trace=True),
            "csv": scenario_entry(parser={"kind": "csv", "header": "a,b", "key": ["a"], "metrics": ["c"], "ignore": []}),
            "regex": scenario_entry(parser={"kind": "regex", "pattern": "(?P<label>.*)"}),
        }
        with tempfile.TemporaryDirectory() as directory:
            for name, entry in cases.items():
                with self.subTest(case=name), self.assertRaises(runtime_profile.ProfileError):
                    runtime_profile.load_manifest(write_manifest(directory, entry))
            with self.assertRaisesRegex(runtime_profile.ProfileError, "repeats a scenario id"):
                runtime_profile.load_manifest(write_manifest(directory, scenario_entry(), scenario_entry()))


class Workflow(unittest.TestCase):
    def test_scheduled_workflow_runs_the_runner_and_keeps_its_baseline_artifact_name(self):
        workflow = (ROOT / ".github/workflows/performance.yml").read_text()
        self.assertRegex(workflow, r"(?m)^  schedule:\n    - cron: ")
        self.assertIn("./scripts/profile-engine", workflow)
        self.assertIn("ARTIFACT: runtime-profile-${{ matrix.lane }}", workflow)
        self.assertIn("name: runtime-profile-${{ matrix.lane }}", workflow)
        self.assertIn("path: out/runtime-profile/${{ matrix.lane }}/", workflow)
        self.assertNotIn("pull_request", workflow, "timings must not become a pull-request gate")


class Selection(unittest.TestCase):
    def setUp(self):
        with tempfile.TemporaryDirectory() as directory:
            self.scenarios = runtime_profile.load_manifest(write_manifest(
                directory,
                scenario_entry(id="plain", cmake_options={"LUDUS_BUILD_A": "ON"}),
                scenario_entry(id="window", requires=["shader-tools", "presentation"],
                               cmake_options={"LUDUS_BUILD_A": "ON", "LUDUS_BUILD_B": "ON"})))

    def test_default_selection_skips_missing_capabilities(self):
        selection = runtime_profile.select_scenarios(self.scenarios, [], {"shader-tools"})
        self.assertEqual([(scenario.id, reason) for scenario, reason in selection],
                         [("plain", None), ("window", "missing capability presentation")])

    def test_explicit_selection_requires_capabilities_and_known_ids(self):
        with self.assertRaisesRegex(runtime_profile.ProfileError, "requires unavailable capability"):
            runtime_profile.select_scenarios(self.scenarios, ["window"], set())
        with self.assertRaisesRegex(runtime_profile.ProfileError, "unknown scenario"):
            runtime_profile.select_scenarios(self.scenarios, ["missing"], set())

    def test_configure_arguments_merge_options_and_shader_tools(self):
        self.assertEqual(runtime_profile.configure_arguments(self.scenarios[:1], ["-DLUDUS_SLANG_COMPILER=x"]),
                         ["-DLUDUS_BUILD_A=ON"])
        self.assertEqual(runtime_profile.configure_arguments(self.scenarios, ["-DLUDUS_SLANG_COMPILER=x"]),
                         ["-DLUDUS_BUILD_A=ON", "-DLUDUS_BUILD_B=ON", "-DLUDUS_SLANG_COMPILER=x"])
        conflicting = [self.scenarios[0], runtime_profile.Scenario(**{
            **self.scenarios[0].__dict__, "id": "other", "cmake_options": (("LUDUS_BUILD_A", "OFF"),)})]
        with self.assertRaisesRegex(runtime_profile.ProfileError, "disagree"):
            runtime_profile.configure_arguments(conflicting, None)


class Parsers(unittest.TestCase):
    def test_key_value_uses_identity_paths(self):
        metrics = runtime_profile.parse_key_value(THREADING_OUTPUT, ["iterations", "workers"], ["median_ns"])
        self.assertEqual(metrics, {
            "iterations=0/workers=0/median_ns": 1833.0,
            "iterations=0/workers=4/median_ns": 27167.0,
            "iterations=10000/workers=4/median_ns": 803750.0,
        })

    def test_csv_reads_the_declared_table_and_ignores_rows(self):
        metrics = runtime_profile.parse_csv(STRINGS_OUTPUT, "case,ns_per_operation", ["case"], ["ns_per_operation"],
                                            ["checksum"])
        self.assertEqual(metrics, {"case=unique_construct_assign/ns_per_operation": 26.0,
                                   "case=xxh3_64/ns_per_operation": 3.0})
        containers = runtime_profile.parse_csv(CONTAINER_OUTPUT, CONTAINER_OUTPUT.splitlines()[0], ["count"],
                                               ["hash_lookup_ns"], [])
        self.assertEqual(containers, {"count=1/hash_lookup_ns": 7.35, "count=16/hash_lookup_ns": 6.03})
        with self.assertRaisesRegex(runtime_profile.ProfileError, "header"):
            runtime_profile.parse_csv("other\n", "case,value", ["case"], ["value"], [])

    def test_regex_labels_lines(self):
        manifest = {scenario.id: scenario for scenario in runtime_profile.load_manifest(ROOT / runtime_profile.MANIFEST_PATH)}
        metrics = runtime_profile.parse_regex(INPUT_OUTPUT, manifest["input-reducer"].parser["pattern"])
        self.assertEqual(metrics, {
            "idle step/median_ns": 42.0, "idle step/p95_ns": 42.0,
            "256 transitions/256 bindings (map=Ok)/median_ns": 20292.0,
            "256 transitions/256 bindings (map=Ok)/p95_ns": 20791.0,
        })

    def test_repeated_or_non_finite_metrics_are_rejected(self):
        with self.assertRaisesRegex(runtime_profile.ProfileError, "repeats metric"):
            runtime_profile.parse_key_value("workers=1 median_ns=1\nworkers=1 median_ns=2\n", ["workers"], ["median_ns"])
        with self.assertRaisesRegex(runtime_profile.ProfileError, "non-finite"):
            runtime_profile.parse_key_value("workers=1 median_ns=nan\n", ["workers"], ["median_ns"])

    def test_perfetto_scopes_and_frames(self):
        metrics = runtime_profile.parse_perfetto_scopes(trace_document())
        self.assertEqual(metrics["scope=Tick/count"], 3.0)
        self.assertEqual(metrics["scope=Tick/median_ns"], 4000.0)
        self.assertEqual(metrics["scope=Motion/median_ns"], 1000.0)
        self.assertEqual(metrics["frames/count"], 3.0)
        self.assertEqual(metrics["frames/interval_median_ns"], 10000.0)
        complete = runtime_profile.parse_perfetto_scopes([{"pid": 1, "tid": 0, "ts": 0, "ph": "X", "dur": 2, "name": "Job"}])
        self.assertEqual(complete["scope=Job/median_ns"], 2000.0)

    def test_perfetto_rejects_misnested_or_empty_traces(self):
        document = trace_document()
        document["traceEvents"][2], document["traceEvents"][3] = document["traceEvents"][3], document["traceEvents"][2]
        with self.assertRaisesRegex(runtime_profile.ProfileError, "innermost"):
            runtime_profile.parse_perfetto_scopes(document)
        with self.assertRaisesRegex(runtime_profile.ProfileError, "no scopes"):
            runtime_profile.parse_perfetto_scopes({"traceEvents": []})


class FakeRunner:
    """Stands in for subprocess.run; writes requested traces and returns scripted outputs."""

    def __init__(self, outputs, returncode=0, trace=None, timeout=False):
        self.outputs = list(outputs)
        self.returncode = returncode
        self.trace = trace
        self.timeout = timeout
        self.commands = []

    def __call__(self, command, **options):
        self.commands.append(command)
        if self.timeout:
            raise subprocess.TimeoutExpired(command, options["timeout"], output="partial")
        if "--trace-output" in command and self.trace is not None:
            Path(command[command.index("--trace-output") + 1]).write_text(json.dumps(self.trace))
        output = self.outputs.pop(0) if len(self.outputs) > 1 else self.outputs[0]
        return subprocess.CompletedProcess(command, self.returncode, stdout=output)


class Execution(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.build = self.directory / "build"
        (self.build / "bin").mkdir(parents=True)
        (self.build / "bin/sample").write_text("")
        self.output = self.directory / "report"

    def scenario(self, **overrides):
        manifest = write_manifest(self.directory, scenario_entry(**overrides))
        return runtime_profile.load_manifest(manifest)[0]

    def test_measured_repetitions_are_summarized(self):
        scenario = self.scenario(warmup=1)
        runner = FakeRunner(["workers=1 median_ns=10\n", "workers=1 median_ns=30\n", "workers=1 median_ns=20\n",
                             "workers=1 median_ns=40\n"])
        result = runtime_profile.run_scenario(scenario, self.build, self.output, runner=runner)
        self.assertEqual(result["status"], "passed")
        self.assertEqual(len(runner.commands), 4)
        metric = result["metrics"]["workers=1/median_ns"]
        self.assertEqual(metric["samples"], [30.0, 20.0, 40.0], "warmup output must not be measured")
        self.assertEqual((metric["median"], metric["min"], metric["max"]), (30.0, 20.0, 40.0))
        self.assertIn("process/wall_ns", result["metrics"])
        self.assertEqual(len(result["logs"]), 4)

    def test_trace_scenarios_pass_an_output_path_and_keep_one_trace(self):
        scenario = self.scenario(trace=True, parser={"kind": "perfetto-scopes"}, arguments=["--headless"])
        runner = FakeRunner([""], trace=trace_document())
        result = runtime_profile.run_scenario(scenario, self.build, self.output, runner=runner)
        self.assertEqual(result["status"], "passed", result.get("reason"))
        self.assertEqual(runner.commands[0][1:3], ["--headless", "--trace-output"])
        self.assertEqual(result["trace"], "traces/sample.json")
        self.assertTrue((self.output / "traces/sample.json").is_file())

    def test_failures_are_reported_not_raised(self):
        cases = {
            "exited with 3": (self.scenario(), FakeRunner(["workers=1 median_ns=1\n"], returncode=3)),
            "exceeded 10s": (self.scenario(), FakeRunner([""], timeout=True)),
            "no metrics parsed": (self.scenario(), FakeRunner(["unrelated\n"])),
            "requested trace": (self.scenario(trace=True, parser={"kind": "perfetto-scopes"}), FakeRunner([""])),
            "different metric set": (self.scenario(), FakeRunner(["workers=1 median_ns=1\n",
                                                                  "workers=2 median_ns=1\n"])),
        }
        for reason, (scenario, runner) in cases.items():
            with self.subTest(reason=reason):
                result = runtime_profile.run_scenario(scenario, self.build, self.output, runner=runner)
                self.assertEqual(result["status"], "failed")
                self.assertIn(reason, result["reason"])
        (self.build / "bin/sample").unlink()
        result = runtime_profile.run_scenario(self.scenario(), self.build, self.output, runner=FakeRunner([""]))
        self.assertIn("was not built", result["reason"])

    def test_reports_and_refuses_foreign_output_directories(self):
        scenario = self.scenario()
        report = runtime_profile.run_scenarios(
            ROOT, "test-profile", self.build, self.output, [(scenario, None)], set(), compiler="clang",
            runner=FakeRunner(["workers=1 median_ns=5\n"]))
        self.assertEqual(report["timing_policy"], "report-only")
        written = json.loads((self.output / "report.json").read_text())
        self.assertEqual(written["scenarios"][0]["status"], "passed")
        self.assertIn("`sample` | passed", (self.output / "summary.md").read_text())
        self.assertFalse((self.output / "work").exists())
        foreign = self.directory / "foreign"
        foreign.mkdir()
        (foreign / "keep.txt").write_text("user data")
        with self.assertRaisesRegex(runtime_profile.ProfileError, "not written by profile-engine"):
            runtime_profile.run_scenarios(ROOT, "test-profile", self.build, foreign, [(scenario, None)], set(),
                                          compiler="clang", runner=FakeRunner(["workers=1 median_ns=5\n"]))
        self.assertTrue((foreign / "keep.txt").is_file())


class Comparison(unittest.TestCase):
    @staticmethod
    def report(metrics):
        return {"schema": 1, "host": {}, "scenarios": [{"id": "sample", "status": "passed", "metrics": {
            name: runtime_profile.summarize(samples) for name, samples in metrics.items()}}]}

    def test_notable_requires_threshold_and_separated_ranges(self):
        baseline = self.report({"a_ns": [100, 101, 102], "b_ns": [100, 130, 160], "c_ns": [100, 101, 102]})
        current = self.report({"a_ns": [150, 151, 152], "b_ns": [140, 150, 160], "c_ns": [104, 105, 106]})
        deltas = runtime_profile.compare(current, baseline)["sample"]
        self.assertTrue(deltas["a_ns"]["notable"])
        self.assertFalse(deltas["b_ns"]["notable"], "overlapping repetition ranges are noise")
        self.assertFalse(deltas["c_ns"]["notable"], "below the relative notice threshold")

    def test_trace_metrics_need_a_change_above_clock_resolution(self):
        baseline = self.report({"scope=Tick/median_ns": [208, 209, 209], "scope=Frame/median_ns": [1000, 1000, 1000]})
        current = self.report({"scope=Tick/median_ns": [250, 250, 250], "scope=Frame/median_ns": [2000, 2000, 2000]})
        deltas = runtime_profile.compare(current, baseline)["sample"]
        self.assertFalse(deltas["scope=Tick/median_ns"]["notable"])
        self.assertTrue(deltas["scope=Frame/median_ns"]["notable"])

    def test_failed_scenarios_are_not_compared(self):
        baseline = self.report({"a_ns": [100]})
        current = copy.deepcopy(baseline)
        current["scenarios"][0]["status"] = "failed"
        self.assertEqual(runtime_profile.compare(current, baseline), {})


if __name__ == "__main__":
    unittest.main()
