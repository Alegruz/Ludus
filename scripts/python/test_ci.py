"""Exercise locked dependency preparation and bounded analysis failure handling."""
from __future__ import annotations

import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

import engine
import web_build


class NativePrerequisiteTests(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]

    def test_rendered_editor_setup_installs_software_vulkan_before_initialization(self):
        action = (self.ROOT / ".github/actions/native-setup/action.yml").read_text()
        step = action.split("    - name: Install native prerequisites\n", 1)[1]
        command = step.split("      run: |\n", 1)[1].split("    - name:", 1)[0]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, body in (("timeout", 'shift 3; exec "$@"'), ("sudo", 'exec "$@"'),
                               ("apt-get", 'printf "%s\n" "$@"')):
                tool = root / name
                tool.write_text("#!/bin/sh\n" + body + "\n")
                tool.chmod(0o755)
            for editor in ("false", "true"):
                environment = {**os.environ, "PATH": str(root), "WITH_EDITOR": editor}
                result = subprocess.run(["/bin/bash", "-e", "-c", command], env=environment,
                                        capture_output=True, text=True, check=True)
                arguments = result.stdout.splitlines()
                for package in ("qt6-base-dev", "qt6-declarative-dev", "qt6-wayland", "libvulkan1", "mesa-vulkan-drivers"):
                    self.assertEqual(package in arguments, editor == "true", package)

    @unittest.skipUnless(shutil.which("timeout"), "GNU timeout is provided by the Linux CI runner")
    def test_silent_package_update_has_a_wall_clock_deadline(self):
        action = (self.ROOT / ".github/actions/native-setup/action.yml").read_text()
        command = next(line.strip() for line in action.splitlines()
                       if "timeout --signal=TERM" in line and "update -qq" in line)
        command = command.replace("5m", "0.1s").replace("--kill-after=30s", "--kill-after=0.1s")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, body in (("sudo", 'exec "$@"'), ("apt-get", "exec /bin/sleep 30")):
                tool = root / name
                tool.write_text("#!/bin/sh\n" + body + "\n")
                tool.chmod(0o755)
            environment = {**os.environ, "PATH": str(root) + os.pathsep + os.environ["PATH"]}
            result = subprocess.run(["/bin/bash", "-c", "apt_options=();\n" + command],
                                    env=environment, capture_output=True, text=True, timeout=3)
            self.assertEqual(result.returncode, 124, result.stdout + result.stderr)

    def test_macos_cache_launchers_survive_restricted_consumer_path(self):
        action = (self.ROOT / ".github/actions/macos-setup/action.yml").read_text()
        command = action.split("    - name: Configure compiler caching\n", 1)[1]
        command = command.split("      run: |\n", 1)[1].split("    - name:", 1)[0]
        with tempfile.TemporaryDirectory(prefix="cache launcher ") as temporary:
            root = Path(temporary)
            tool = root / "ccache"
            tool.write_text('#!/bin/sh\ncase "$1" in --max-size=*|--zero-stats) exit 0;; esac\nexec "$@"\n')
            tool.chmod(0o755)
            environment_file = root / "github-env"
            environment = {**os.environ, "PATH": str(root), "GITHUB_ENV": str(environment_file),
                           "GITHUB_WORKSPACE": str(root)}
            subprocess.run(["/bin/bash", "-e", "-c", command], env=environment, check=True)
            exported = dict(line.split("=", 1) for line in environment_file.read_text().splitlines())
            for language in ("C", "CXX", "OBJCXX"):
                launcher = exported[f"CMAKE_{language}_COMPILER_LAUNCHER"]
                self.assertEqual(launcher, str(tool))
                # Release/relocation tests deliberately exclude Homebrew's bin
                # directory. The launcher must work without searching PATH.
                result = subprocess.run([launcher, "/bin/echo", "compiler-ran"],
                                        env={"PATH": "/usr/bin:/bin"}, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), "compiler-ran")

    def test_cache_report_survives_missing_prerequisite_and_reports_when_available(self):
        workflow = (self.ROOT / ".github/workflows/ci.yml").read_text()
        commands = [line.strip().removeprefix("run: ") for line in workflow.splitlines()
                    if "ccache --show-stats" in line]
        self.assertTrue(commands)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            summary = root / "summary"
            environment = {**os.environ, "PATH": str(root), "GITHUB_STEP_SUMMARY": str(summary)}
            for command in commands:
                result = subprocess.run(["/bin/bash", "-c", command], env=environment, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(summary.exists())
            tool = root / "ccache"
            tool.write_text('#!/bin/sh\n[ "$1" = --show-stats ] || exit 1\necho cache-statistics\n')
            tool.chmod(0o755)
            result = subprocess.run(["/bin/bash", "-c", commands[0]], env=environment, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(summary.read_text(), "cache-statistics\n")


class LockedSetupTests(unittest.TestCase):
    def test_missing_system_tools_fail_before_venv_or_network(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
            root = Path(temporary)
            stack.enter_context(patch.object(engine, "check_current_python"))
            shims = stack.enter_context(patch.object(engine, "configure_system_tool_shims"))
            stack.enter_context(patch.object(engine, "validate_required_system_tools",
                                            side_effect=engine.EngineError("missing Clang")))
            setup = stack.enter_context(patch.object(engine, "create_or_update_venv"))
            install = stack.enter_context(patch.object(engine, "install_managed_tools"))
            run = stack.enter_context(patch.object(engine, "run"))
            versions = {"minimum": {"python": "3.10"}}
            with self.assertRaisesRegex(engine.EngineError, "missing Clang"):
                engine.prepare_conan_artifacts(root, versions, (engine.DEFAULT_PRESET,))
            shims.assert_called_once_with(root, versions)
            setup.assert_not_called()
            install.assert_not_called()
            run.assert_not_called()

    def test_unlocked_setup_preserves_other_hosts_and_failed_refresh_preserves_lock(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock = root / "conan.lock"
            original = b'{"version":"0.5","requires":["volk/linux-pin"]}\n'
            lock.write_bytes(original)
            merged = b'{"version":"0.5","requires":["volk/linux-pin","freetype/mac-pin"]}\n'

            def execute(command, **kwargs):
                self.assertEqual(lock.read_bytes(), original)
                if command[2] == "create":
                    self.assertIn("--lockfile=", command)
                    Path(command[command.index("--lockfile-out") + 1]).write_text("new graph")
                else:
                    self.assertEqual(command[2], "merge")
                    Path(command[command.index("--lockfile-out") + 1]).write_bytes(merged)

            with patch.object(engine, "tool_env", return_value={}), patch.object(engine, "run", side_effect=execute):
                engine.create_conan_lock(root, root / "profile")
            self.assertEqual(lock.read_bytes(), merged)
            for fail_on in ("create", "merge"):
                lock.write_bytes(original)

                def fail(command, **kwargs):
                    if command[2] == fail_on:
                        raise engine.EngineError("refresh failed")
                    execute(command, **kwargs)

                with self.subTest(command=fail_on), patch.object(engine, "tool_env", return_value={}), \
                        patch.object(engine, "run", side_effect=fail):
                    with self.assertRaisesRegex(engine.EngineError, "refresh failed"):
                        engine.create_conan_lock(root, root / "profile")
                self.assertEqual(lock.read_bytes(), original)

    def test_locked_setup_preserves_lock_and_prepares_only_selected_preset(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.ExitStack() as stack:
            root = Path(temporary)
            lock = root / "conan.lock"
            lock.write_bytes(b'{"version": "0.5", "requires": []}\n')
            before = lock.read_bytes()
            for name in ("check_current_python", "create_or_update_venv", "install_managed_tools",
                         "validate_managed_tools", "configure_system_tool_shims", "validate_required_system_tools",
                         "export_local_recipes", "run"):
                stack.enter_context(patch.object(engine, name))
            stack.enter_context(patch.object(engine, "install_conan_profile", return_value=root / "profile"))
            regenerate = stack.enter_context(patch.object(engine, "create_conan_lock"))
            install = stack.enter_context(patch.object(engine, "conan_install_for_preset"))
            engine.prepare_conan_artifacts(root, {"minimum": {"python": "3.10"}},
                                           (engine.DEFAULT_PRESET,), locked=True)
            regenerate.assert_not_called()
            install.assert_called_once_with(root, root / "profile", engine.DEFAULT_PRESET, locked=True)
            self.assertEqual(lock.read_bytes(), before)

    def test_missing_lock_fails_before_tools_or_network(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(engine, "run") as run, \
                patch.object(engine, "create_or_update_venv") as setup:
            with self.assertRaisesRegex(engine.EngineError, "committed conan.lock"):
                engine.prepare_conan_artifacts(Path(temporary), {}, (engine.DEFAULT_PRESET,), locked=True)
            setup.assert_not_called()
            run.assert_not_called()

    def test_locked_install_does_not_allow_unlocked_dependencies(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(engine, "run") as run, \
                patch.object(engine, "tool_env", return_value={}):
            root = Path(temporary)
            engine.conan_install_for_preset(root, root / "profile", engine.DEFAULT_PRESET, locked=True)
            command = run.call_args.args[0]
            self.assertIn("--lockfile", command)
            self.assertNotIn("--lockfile-partial", command)
            self.assertEqual(command[command.index("--lockfile") + 1], str(root / "conan.lock"))


class AnalysisTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    @staticmethod
    def result(code, text, log):
        from quality_process import ProcessResult
        log.write_text(text)
        return ProcessResult(code, log, len(text.encode()), False, False)

    def test_shards_execute_every_translation_unit_once(self):
        commands = [["tidy", str(i)] for i in range(11)]
        visited = []

        def analyze(command, **kwargs):
            visited.append(command[1])
            return self.result(0, "", kwargs["log"])

        with patch.dict(os.environ, {"LUDUS_TIDY_JOBS": "2", "LUDUS_TIDY_SHARD_COUNT": "3"}), \
                patch.object(engine, "run_bounded", side_effect=analyze), \
                contextlib.redirect_stdout(io.StringIO()):
            for index in range(3):
                with patch.dict(os.environ, {"LUDUS_TIDY_SHARD_INDEX": str(index)}):
                    engine.run_analysis_commands(self.root, commands)
        self.assertCountEqual(visited, [str(i) for i in range(11)])

    def test_invalid_or_empty_shards_fail_before_launching_analysis(self):
        for count, index in (("0", "0"), ("2", "2"), ("2", "-1"), ("two", "0"), ("2", "one"), ("2", "1")):
            with self.subTest(count=count, index=index), \
                    patch.dict(os.environ, {"LUDUS_TIDY_SHARD_COUNT": count, "LUDUS_TIDY_SHARD_INDEX": index}), \
                    patch.object(engine, "run_bounded") as run:
                with self.assertRaises(engine.EngineError):
                    engine.run_analysis_commands(self.root, [["tidy", "source.cpp"]])
                run.assert_not_called()

    def test_failed_shard_keeps_diagnostics_and_fails_the_gate(self):
        commands = [[sys.executable, "-c", "print('other shard')"],
                    [sys.executable, "-c", "import sys; print('failed shard'); sys.exit(1)"]]
        output = io.StringIO()
        with patch.dict(os.environ, {"LUDUS_TIDY_SHARD_COUNT": "2", "LUDUS_TIDY_SHARD_INDEX": "1"}), \
                contextlib.redirect_stdout(output):
            with self.assertRaisesRegex(engine.EngineError, "1 translation unit"):
                engine.run_analysis_commands(self.root, commands)
        self.assertIn("failed shard\n", output.getvalue())
        self.assertNotIn("other shard", output.getvalue())

    def test_empty_database_cannot_pass_a_sharded_gate(self):
        with patch.dict(os.environ, {"LUDUS_TIDY_SHARD_COUNT": "2", "LUDUS_TIDY_SHARD_INDEX": "0"}), \
                patch.object(engine, "run_bounded") as run:
            with self.assertRaisesRegex(engine.EngineError, "no translation units"):
                engine.run_analysis_commands(self.root, [])
            run.assert_not_called()

    def test_concurrency_is_bounded_and_all_failures_are_reported(self):
        barrier = threading.Barrier(2)
        mutex = threading.Lock()
        active = 0
        peak = 0
        visited = []

        def analyze(command, **kwargs):
            nonlocal active, peak
            with mutex:
                active += 1
                peak = max(peak, active)
                visited.append(command[1])
            barrier.wait(timeout=5)
            with mutex:
                active -= 1
            return self.result(1 if command[1] in ("0", "3") else 0,
                               f"diagnostic {command[1]}\n", kwargs["log"])

        output = io.StringIO()
        with patch.dict(os.environ, {"LUDUS_TIDY_JOBS": "2"}), \
                patch.object(engine, "run_bounded", side_effect=analyze), contextlib.redirect_stdout(output):
            with self.assertRaisesRegex(engine.EngineError, "2 translation unit"):
                engine.run_analysis_commands(self.root, [["tidy", str(i)] for i in range(4)])
        self.assertEqual(peak, 2)
        self.assertCountEqual(visited, ["0", "1", "2", "3"])
        diagnostics = [line for line in output.getvalue().splitlines() if line.startswith("diagnostic")]
        self.assertEqual(diagnostics, [f"diagnostic {i}" for i in range(4)])

    def test_invalid_worker_count_fails_without_launching_analysis(self):
        for value in ("0", "-1", "two"):
            with self.subTest(value=value), patch.dict(os.environ, {"LUDUS_TIDY_JOBS": value}), \
                    patch.object(engine, "run_bounded") as run:
                with self.assertRaisesRegex(engine.EngineError, "positive integer"):
                    engine.run_analysis_commands(self.root, [["tidy", "source.cpp"]])
                run.assert_not_called()

    def test_real_process_diagnostics_survive_a_failed_command(self):
        commands = [
            [sys.executable, "-c", "import sys; print('analysis error', file=sys.stderr); sys.exit(2)"],
            [sys.executable, "-c", "print('analysis completed')"],
        ]
        output = io.StringIO()
        with patch.dict(os.environ, {"LUDUS_TIDY_JOBS": "2"}), contextlib.redirect_stdout(output):
            with self.assertRaisesRegex(engine.EngineError, "1 translation unit"):
                engine.run_analysis_commands(self.root, commands)
        self.assertIn("analysis error\n", output.getvalue())
        self.assertIn("analysis completed\n", output.getvalue())
        self.assertIn("Analysis exited with code 2", output.getvalue())

    def test_missing_analyzer_reports_actionable_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(engine.EngineError, "Cannot launch analysis"):
                engine.run_analysis_commands(Path(temporary), [[Path(temporary) / "missing-analyzer"]])

    def test_missing_worker_result_cannot_pass(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(engine, "ThreadPoolExecutor") as pool:
            pool.return_value.__enter__.return_value.map.return_value = []
            with self.assertRaisesRegex(engine.EngineError, "omitted required"):
                engine.run_analysis_commands(self.root, [["tidy", "source.cpp"]])
            pool.assert_called_once_with(max_workers=1)


class BrowserAnalysisTests(unittest.TestCase):
    def test_only_explicit_owned_generated_file_is_analyzed(self):
        root = Path("/tmp/browser-analysis").resolve()
        sources = [root / "out/script-interaction/cooked/bindings.cpp", root / "out/vendor.cpp"]
        entries = [{"file": str(source), "command": f"em++ -c {source} -o object.o"}
                   for source in sources]
        commands = web_build.analysis_commands(root, entries, "clang-tidy-18", root / "sysroot",
                                              owned_generated=(sources[0],))
        self.assertEqual([command[2] for command in commands], [str(sources[0])])

    def test_owns_engine_apps_and_probes_but_not_vendor_or_generated_sources(self):
        root = Path("/tmp/browser-analysis").resolve()
        sources = ["modules/audio/src/audio.cpp", "apps/smoke/main.cpp", "tools/web-rhi-probe/main.cpp",
                   "tests/sdk_consumer/strings.cpp", "third_party/yyjson/yyjson.c", "out/generated.cpp",
                   "modules-other/file.cpp"]
        entries = [{"file": name, "directory": str(root),
                    "command": f"em++ -std=c++23 --use-port=emdawnwebgpu -DWEB=1 -c {name} -o object.o"}
                   for name in sources]
        commands = web_build.analysis_commands(root, entries, "clang-tidy-18", root / "sysroot")
        self.assertEqual([command[2] for command in commands], [str(root / source) for source in sources[:4]])
        for command in commands:
            self.assertIn("--warnings-as-errors=*", command)
            self.assertIn("--target=wasm32-unknown-emscripten", command)
            self.assertIn("-DWEB=1", command)
            self.assertIn("-frelaxed-template-template-args", command)
            self.assertNotIn("object.o", command)
            self.assertNotIn("-c", command)
            self.assertNotIn("--use-port=emdawnwebgpu", command)

    def test_argument_arrays_and_paths_with_spaces_are_preserved(self):
        root = Path("/tmp/browser project").resolve()
        source = str(root / "modules/base/src/main.cpp")
        entries = [{"file": source, "arguments": ["em++", "-I", str(root / "include"), "-c", source, "-o", "main.o"]}]
        command = web_build.analysis_commands(root, entries, "clang-tidy-18", root / "sysroot")[0]
        self.assertEqual(command[2], source)
        self.assertIn(str(root / "include"), command)


class ShaderBootstrapCacheTests(unittest.TestCase):
    def test_relocated_verified_tool_needs_no_download_or_cmake_build(self):
        script = Path(engine.__file__).resolve().parents[2] / "scripts/bootstrap-spirv-cross"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            copy = root / "scripts/bootstrap-spirv-cross"
            copy.parent.mkdir()
            copy.write_bytes(script.read_bytes())
            binary = root / "out/shader-tools/spirv-cross/bin/spirv-cross"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"verified translator")
            (binary.parent.parent / "LICENSE").touch()
            archive = root / "out/shader-tools/spirv-cross-src/spirv-cross.tar.gz"
            archive.parent.mkdir()
            archive.write_bytes(b"verified source archive")
            pin = {"tag": "pinned-tag", "commit": "pinned-revision",
                   "source_sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}
            (root / "config").mkdir()
            (root / "config/spirv_cross_toolchain.json").write_text(json.dumps({"spirv_cross": pin}))
            manifest = {**pin, "compiler": "clang version 18.1.8", "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
            Path(str(binary) + ".build.json").write_text(json.dumps(manifest))
            namespace = runpy.run_path(str(copy))
            with patch.object(subprocess, "check_output", return_value="clang version 18.1.8\n"), \
                    patch.object(subprocess, "run") as build, \
                    patch("urllib.request.urlretrieve") as download, contextlib.redirect_stdout(io.StringIO()):
                namespace["main"]()
            build.assert_not_called()
            download.assert_not_called()
            check = namespace["cached_translator"]
            self.assertFalse(check(binary.parent.parent, {**pin, "commit": "different"}, "clang version 18.1.8"))
            self.assertFalse(check(binary.parent.parent, pin, "Clang 19"))
            binary.write_bytes(b"modified translator")
            self.assertFalse(check(binary.parent.parent, pin, "clang version 18.1.8"))


if __name__ == "__main__":
    unittest.main()
