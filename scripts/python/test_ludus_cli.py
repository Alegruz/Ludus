"""Integration tests for the ludus CLI (shared backend, P07).

    python3 -m unittest test_ludus_cli -v

These exercise the installed-CLI surface end to end against a FAKE SDK (a tar of
a minimal prefix carrying a valid manifest). They prove the create/sdk/engine/
resolution logic and the exit-code contract. The actual configure/build/run of
generated C++ requires the pinned native toolchain and is UNAVAILABLE here
(tracked in docs/development/project-sdk-workflow-evidence.md).
"""

from __future__ import annotations

import io
import json
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

import ludus_tools.cli as cli
from ludus_tools.errors import EXIT_OK, EXIT_UNAVAILABLE

from test_ludus_tools import _make_sdk_tar, _manifest_json


class CliFlow(unittest.TestCase):
    def setUp(self) -> None:
        self.td = tempfile.TemporaryDirectory()
        self.root = Path(self.td.name)
        self.store = self.root / "store"
        self.tar = self.root / "sdk-dev.tar.gz"
        _make_sdk_tar(self.tar, _manifest_json(build_flavor="Development"))

    def tearDown(self) -> None:
        self.td.cleanup()

    def _run(self, *argv) -> int:
        return cli.main(list(argv))

    def _run_json(self, *argv) -> dict:
        buf = io.StringIO()
        with redirect_stdout(buf):
            code = cli.main(["--json", *argv])
        self.assertNotEqual(buf.getvalue().strip(), "")
        return {"code": code, **json.loads(buf.getvalue().strip().splitlines()[-1])}

    def test_install_list_and_create(self) -> None:
        self.assertEqual(self._run("--store", str(self.store), "sdk", "install", "--archive", str(self.tar)), EXIT_OK)
        out = self._run_json("--store", str(self.store), "sdk", "list")
        self.assertEqual(out["code"], EXIT_OK)
        self.assertEqual(len(out["sdks"]), 1)
        self.assertEqual(out["sdks"][0]["flavor"], "Development")

        g1 = self.root / "GameOne"
        self.assertEqual(self._run("project", "create", str(g1), "--name", "GameOne", "--engine", "0.1.0"), EXIT_OK)
        self.assertTrue((g1 / "ludus.project.json").is_file())

    def test_two_projects_one_sdk(self) -> None:
        self._run("--store", str(self.store), "sdk", "install", "--archive", str(self.tar))
        prefix = next((self.store / "sdks").iterdir())
        g1 = self.root / "G1"
        g2 = self.root / "G2"
        self._run("project", "create", str(g1), "--name", "G1", "--engine", "0.1.0")
        self._run("project", "create", str(g2), "--name", "G2", "--engine", "0.1.0")
        self._run("--store", str(self.store), "project", "engine", str(g1), "--sdk", str(prefix))
        self._run("--store", str(self.store), "project", "engine", str(g2), "--sdk", str(prefix))
        # One shared immutable SDK directory.
        self.assertEqual(len(list((self.store / "sdks").iterdir())), 1)
        # Each project has its own ignored override; committed locks unchanged.
        for g in (g1, g2):
            local = json.loads((g / ".ludus" / "local.json").read_text())
            self.assertEqual(local["sdk_overrides"][0]["prefix"], str(prefix.resolve()))
            lock = json.loads((g / "ludus.lock.json").read_text())
            self.assertFalse(lock["resolved"])

    def test_clear_override_restores_locked_resolution(self) -> None:
        self._run("--store", str(self.store), "sdk", "install", "--archive", str(self.tar))
        prefix = next((self.store / "sdks").iterdir())
        g = self.root / "G"
        self._run("project", "create", str(g), "--name", "G", "--engine", "0.1.0")
        self._run("--store", str(self.store), "project", "engine", str(g), "--sdk", str(prefix))
        self.assertEqual(len(json.loads((g / ".ludus" / "local.json").read_text())["sdk_overrides"]), 1)
        self._run("--store", str(self.store), "project", "engine", str(g), "--clear-override")
        self.assertEqual(len(json.loads((g / ".ludus" / "local.json").read_text())["sdk_overrides"]), 0)
        # Committed descriptor + lock are untouched by override set/clear.
        d = json.loads((g / "ludus.project.json").read_text())
        self.assertEqual(d["engine"]["version"], "0.1.0")

    def test_build_unresolved_lock_is_unavailable(self) -> None:
        # No override + unresolved lock => an actionable UNAVAILABLE exit, never
        # a fallback engine build.
        g = self.root / "G"
        self._run("project", "create", str(g), "--name", "G", "--engine", "0.1.0")
        code = self._run("--store", str(self.store), "project", "build", str(g))
        self.assertEqual(code, EXIT_UNAVAILABLE)

    def test_migrate_then_resolves_v2(self) -> None:
        g = self.root / "legacy"
        g.mkdir()
        (g / "ludus.project.json").write_text(json.dumps({
            "version": 1, "name": "Legacy", "provider": "cmake", "source_dir": ".",
            "preset": "linux-clang-development", "target": "legacy_app",
            "run": {"cwd": ".", "args": []},
        }))
        self.assertEqual(self._run("project", "migrate", str(g), "--engine", "0.1.0"), EXIT_OK)
        d = json.loads((g / "ludus.project.json").read_text())
        self.assertEqual(d["version"], 2)
        self.assertEqual(d["engine"]["version"], "0.1.0")

    def test_create_requires_engine_or_sdk(self) -> None:
        g = self.root / "G"
        code = self._run("project", "create", str(g), "--name", "G")
        self.assertNotEqual(code, EXIT_OK)
        self.assertFalse(g.exists())


if __name__ == "__main__":
    unittest.main()
