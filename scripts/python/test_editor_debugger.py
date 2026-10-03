"""RAD game-debugging regressions: real ELF files and owned child processes."""
from __future__ import annotations

import json
import os
import shutil
import signal
import subprocess
import sys
import unittest
import selectors
import time
from pathlib import Path
from unittest.mock import patch

import editor_debugger
import engine
import rad_debugger as rad
from ludus_tools.errors import ToolingError
import test_editor_tool as fixtures


class DebugClient(fixtures.ProtocolClient):
    def __init__(self, context):
        self.buffer = b""
        self.stdin_closed = False
        super().__init__(context)

    def close_stdin(self):
        if not self.stdin_closed:
            self.stdin_closed = True
            super().close_stdin()

    def read_event(self, timeout=20):
        deadline = time.monotonic() + timeout
        os.set_blocking(self._out_read, False)
        with selectors.DefaultSelector() as selector:
            selector.register(self._out_read, selectors.EVENT_READ)
            while b"\n" not in self.buffer:
                if time.monotonic() >= deadline:
                    raise AssertionError("Timed out waiting for debugger event")
                if selector.select(min(0.1, max(0, deadline - time.monotonic()))):
                    chunk = os.read(self._out_read, 65536)
                    if not chunk:
                        raise AssertionError("Adapter closed before the terminal event")
                    self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(line)

    def cleanup(self):
        self.close_stdin()
        self._thread.join(timeout=10)
        super().cleanup()


class DebuggerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("clang-18"):
            raise unittest.SkipTest("Requires the reference clang-18 to generate native ELF fixtures")

    def setUp(self):
        self.fixture = fixtures.RealProcessTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.addCleanup(self.fixture.tearDown)
        f = self.fixture
        (f.root / "config").mkdir()
        self.pin = rad.pin(engine.repo_root(), engine)
        (f.root / "config/rad_debugger.json").write_text(json.dumps(self.pin))
        source = f.root / "game.c"
        source.write_text('#include <unistd.h>\nint main(void) { for (;;) pause(); }\n')
        self.game = f.root / "game with symbols"
        subprocess.run(["clang-18", "-g", str(source), "-o", str(self.game)], check=True,
                       capture_output=True)
        self.rad = f.root / "RAD executable"
        self.make_rad('print("RAD fixture ready", flush=True)\n')
        self.preference = f.source / ".ludus/debugger.json"
        self.digest = f.write_project()
        display = patch.dict(os.environ, {"DISPLAY": ":fixture"})
        display.start()
        self.addCleanup(display.stop)

    def make_rad(self, body):
        self.rad.write_text('#!' + sys.executable + '\nimport os, sys, json, signal, subprocess, time\n'
                            + body)
        self.rad.chmod(0o755)

    def start(self, *, debugger=None, setup=False, control="", digest=None):
        f = self.fixture
        client = DebugClient(f.context(self.game, control=control))
        f.clients.append(client)
        self.assertEqual(client.read_event()["type"], "ready")
        request = f.request("build_debug", digest or self.digest)
        request.update(debugger=str(self.rad) if debugger is None else debugger, setup_debugger=setup)
        client.send(request)
        return client

    def finish(self, client):
        events = []
        while not events or events[-1]["type"] != "result":
            events.append(client.read_event(timeout=15))
        client.close_stdin()
        client._thread.join(timeout=5)
        return events

    def test_missing_debugger_is_read_only_and_never_builds_or_installs(self):
        events = self.finish(self.start(debugger="/missing/raddbg"))
        self.assertEqual(events[-1]["code"], "MissingDebugger")
        self.assertFalse(any(e["type"] == "command" for e in events))
        self.assertFalse(self.preference.exists())
        self.assertFalse((self.fixture.source / "out/debug").exists())

    def test_saved_external_choice_reuses_session_and_preserves_settings(self):
        first = self.finish(self.start())
        self.assertEqual(first[-1]["outcome"], "success")
        self.assertTrue(any(e["type"] == "debugger_started" for e in first))
        self.assertFalse(any(e["type"] == "runtime_started" for e in first))
        preference = json.loads(self.preference.read_text())
        self.assertEqual(preference["executable"], str(self.rad))
        directory = next((self.fixture.source / "out/debug/rad/linux-clang-debug/app/versions").iterdir())
        settings = directory / "session.raddbg_user"
        settings.write_text("breakpoints and watches\n")
        second = self.finish(self.start(debugger=""))
        self.assertEqual(second[-1]["outcome"], "success")
        self.assertEqual(settings.read_text(), "breakpoints and watches\n")
        description = json.loads((directory / "launch.json").read_text())
        self.assertEqual(description["cwd"], str(self.fixture.source))
        self.assertEqual(description["executable"], str(self.fixture.build / "bin/app"))
        self.assertEqual(description["arguments"], [])

    def test_invalid_saved_preference_does_not_fall_back(self):
        self.preference.parent.mkdir()
        self.preference.write_text(json.dumps({"provider": "rad", "executable": "/missing/rad"}))
        with patch.dict(os.environ, {"LUDUS_RAD_DEBUGGER": str(self.rad)}):
            events = self.finish(self.start(debugger=""))
        self.assertEqual(events[-1]["code"], "MissingDebugger")

    def test_failed_build_never_launches_an_older_artifact(self):
        events = self.finish(self.start(control="build_fail"))
        self.assertEqual(events[-1]["code"], "BuildFailed")
        self.assertFalse(any(e["type"] == "debugger_started" for e in events))

    def test_debug_uses_the_selected_descriptor_path(self):
        project = self.fixture.project
        self.fixture.project = project.with_name("custom-game.project.json")
        project.rename(self.fixture.project)
        events = self.finish(self.start())
        self.assertEqual(events[-1]["outcome"], "success")

    def test_debug_checks_selectable_presets_before_configuring(self):
        with patch("ludus_tools.cmake_setup.validate_project_presets", side_effect=ValueError("missing selectable build preset")):
            events = self.finish(self.start())
        self.assertEqual(events[-1]["code"], "InvalidProject")
        self.assertFalse(any(e["type"] == "command" for e in events))

    def test_stripped_game_fails_before_debugger_spawn(self):
        subprocess.run(["strip", str(self.game)], check=True)
        events = self.finish(self.start())
        self.assertEqual(events[-1]["code"], "ArtifactInvalid")
        self.assertIn("DWARF", events[-1]["message"])
        self.assertFalse(any(e["type"] == "debugger_started" for e in events))

    def test_missing_unwind_header_fails_before_debugger_spawn(self):
        subprocess.run(["objcopy", "--remove-section=.eh_frame_hdr", str(self.game)], check=True)
        events = self.finish(self.start())
        self.assertEqual(events[-1]["code"], "ArtifactInvalid")
        self.assertIn("eh_frame_hdr", events[-1]["message"])
        self.assertFalse(any(e["type"] == "debugger_started" for e in events))

    def test_local_preference_ignores_machine_paths_and_preserves_custom_rules(self):
        ignore = self.fixture.source / ".gitignore"
        ignore.write_text("# project rule\n/custom/\n")
        self.assertEqual(self.finish(self.start())[-1]["outcome"], "success")
        self.assertEqual(ignore.read_text(), "# project rule\n/custom/\n\n/.ludus/\n")
        before = ignore.read_bytes()
        self.assertEqual(self.finish(self.start())[-1]["outcome"], "success")
        self.assertEqual(ignore.read_bytes(), before)

    def test_argument_limitation_fails_before_build_or_install(self):
        self.digest = self.fixture.write_project(["a b"])
        events = self.finish(self.start(setup=True, debugger=""))
        self.assertEqual(events[-1]["code"], "InvalidProject")
        self.assertFalse(any(e["type"] == "command" for e in events))

    def test_exact_argv_cwd_and_unicode_are_preserved_in_session(self):
        self.digest = self.fixture.write_project(["--level", "日本語", "$(literal)"])
        events = self.finish(self.start())
        command = next(e for e in events if e["type"] == "command" and e["stage"] == "launching")
        self.assertEqual(command["argv"][-3:], ["--level", "日本語", "$(literal)"])
        self.assertEqual(command["cwd"], str(self.fixture.source))
        self.assertTrue(command["argv"][command["argv"].index("--") + 1].startswith("./"))

    def test_debugger_abnormal_exit_is_not_a_successful_game_run(self):
        self.make_rad("sys.exit(7)\n")
        events = self.finish(self.start())
        self.assertEqual(events[-1]["code"], "DebuggerFailed")
        self.assertEqual(events[-1]["exit_code"], 7)

    def test_stop_cleans_up_a_paused_game_and_keeps_unrelated_process_alive(self):
        self.make_rad('''args = sys.argv[sys.argv.index("--") + 1:]
game = subprocess.Popen(args)
os.kill(game.pid, signal.SIGSTOP)
def stop(*_):
    game.kill()
    game.wait()
    sys.exit(0)
signal.signal(signal.SIGTERM, stop)
print("PAUSED_GAME:" + str(game.pid), flush=True)
while True: time.sleep(0.05)
''')
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
        self.addCleanup(lambda: (unrelated.terminate(), unrelated.wait()))
        client = self.start()
        events = []
        game_pid = None
        while game_pid is None:
            event = client.read_event(timeout=10)
            events.append(event)
            if event["type"] == "output" and "PAUSED_GAME:" in event["text"]:
                game_pid = int(event["text"].split("PAUSED_GAME:")[1].strip())
        self.assertTrue(any(e["type"] == "debugger_started" for e in events))
        client.send({"protocol": 1, "type": "cancel", "job": "0000000000000001"})
        events += self.finish(client)
        self.assertEqual(events[-1]["outcome"], "cancelled")
        self.assertTrue(events[-1]["cleanup_confirmed"])
        self.assertFalse(Path(f"/proc/{game_pid}").exists())
        self.assertIsNone(unrelated.poll())

    def test_session_lock_interlocks_with_the_cli_policy(self):
        path = self.fixture.source / "out/debug/rad/linux-clang-debug/app"
        with rad.debug_session(self.fixture.root, self.rad, "linux-clang-debug", "app", engine,
                               target_session=path):
            events = self.finish(self.start())
        self.assertEqual(events[-1]["code"], "Busy")
        self.assertFalse(any(e["type"] == "command" for e in events))

    def setup_script(self, body):
        script = self.fixture.root / "scripts/setup-rad-debugger"
        script.parent.mkdir()
        script.write_text('#!' + sys.executable + '\nimport json, sys, os, time\nfrom pathlib import Path\n' + body)
        script.chmod(0o755)

    def test_explicit_setup_continues_debugging_and_uses_the_managed_pin(self):
        root = self.fixture.root
        self.setup_script(f'''assert sys.argv[1:] == ["--no-system-install"]
directory = Path({str(rad.install_dir(root, self.pin))!r})
binary = directory / "source/build/raddbg"
binary.parent.mkdir(parents=True)
binary.write_text("#!/bin/sh\\nexit 0\\n")
binary.chmod(0o755)
(directory / "build.json").write_text(json.dumps({{"pin": {self.pin!r}}}))
''')
        events = self.finish(self.start(debugger="", setup=True))
        self.assertEqual(events[-1]["outcome"], "success")
        self.assertTrue(any(e["type"] == "debugger_started" for e in events))
        self.assertTrue(json.loads(self.preference.read_text())["managed"])

    def test_setup_failure_never_builds_or_changes_preferences(self):
        self.setup_script("sys.exit(9)\n")
        events = self.finish(self.start(debugger="", setup=True))
        self.assertEqual(events[-1]["code"], "DebuggerFailed")
        self.assertEqual(len([e for e in events if e["type"] == "command"]), 1)
        self.assertFalse(self.preference.exists())

    def test_cancel_during_setup_never_continues_to_build(self):
        self.setup_script('print("SETUP_READY", flush=True)\ntime.sleep(30)\n')
        client = self.start(debugger="", setup=True)
        while True:
            event = client.read_event(timeout=10)
            if event["type"] == "output" and "SETUP_READY" in event["text"]:
                break
        client.send({"protocol": 1, "type": "cancel", "job": "0000000000000001"})
        events = self.finish(client)
        self.assertEqual(events[-1]["outcome"], "cancelled")
        self.assertFalse(self.preference.exists())

    def test_stale_descriptor_cannot_trigger_setup(self):
        events = self.finish(self.start(debugger="", setup=True, digest="0" * 64))
        self.assertEqual(events[-1]["code"], "Conflict")
        self.assertFalse(any(e["type"] == "command" for e in events))

    def test_symbol_validator_rejects_non_native_and_truncated_artifacts(self):
        self.game.write_bytes(b"\0asm\1\0\0\0")
        with self.assertRaises(ToolingError):
            editor_debugger.validate_symbols(self.game)


if __name__ == "__main__":
    unittest.main()
