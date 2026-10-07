"""Dependency admission, transitive identity and cache/publication regressions."""
import copy
import json
from pathlib import Path
import runpy
import shutil
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
S2 = runpy.run_path(str(Path(__file__).with_name("session.py")), run_name="s2_test")


class SessionCook(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        shutil.copytree(ROOT / "tools/script-session/fixtures", self.root / "fixtures")
        self.manifest = self.root / "fixtures/catalog.json"
        self.catalog = json.loads(self.manifest.read_text())

    def tearDown(self):
        self.temporary.cleanup()

    def reject(self, data, message):
        self.manifest.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, message): S2["load"](self.manifest)

    def test_closed_manifest_missing_and_dynamic_imports(self):
        data = copy.deepcopy(self.catalog)
        data["packages"][0]["extra"] = True
        self.reject(data, "keys")
        source = self.root / "fixtures/root.luau"
        source.write_text('local x = require(event.Name)\n')
        self.reject(self.catalog, "literal")
        source.write_text('local x = require("0000000000000040")\n')
        data = copy.deepcopy(self.catalog)
        for p in data["packages"][0]["programs"]:
            if p["entrypoint"]: p["dependencies"] = ["0000000000000040"]
        data["packages"] = data["packages"][:1]
        self.reject(data, "missing")

    def test_cycle_and_import_lexing(self):
        self.assertEqual(S2["imports"]('-- require("bad")\nlocal x = "require"\n--[=[ require("bad") ]=]\n'), [])
        data = copy.deepcopy(self.catalog)
        data["packages"] = data["packages"][:1]
        module = data["packages"][0]["programs"][0]
        module["dependencies"] = [module["asset"]]
        (self.root / "fixtures/logic_a.luau").write_text('return require("0000000000000010")\n')
        self.reject(data, "cycle")

    def test_escape_and_duplicate(self):
        data = copy.deepcopy(self.catalog)
        data["packages"][0]["programs"][0]["source"] = "../root.luau"
        self.reject(data, "unsafe")
        data = copy.deepcopy(self.catalog)
        data["packages"][0]["programs"][1]["asset"] = data["packages"][0]["programs"][0]["asset"]
        self.reject(data, "duplicate")

    def test_transitive_cache_and_failure_preserves_previous(self):
        calls = []
        def fake(command, **kwargs):
            calls.append(command)
            return SimpleNamespace(stdout=b"\x06trusted-paired-fixture")
        def cook(directory, cache=None):
            with patch("subprocess.run", side_effect=fake):
                return S2["cook"](self.manifest, directory, Path("compile"), Path("analyze"),
                                   "pin", "--!strict\n", "contract", "tools", cache)
        first = cook(self.root / "first")
        self.assertEqual(len([c for c in calls if str(c[0]) == "compile"]), 12)
        calls.clear()
        second = cook(self.root / "second", self.root / "first")
        self.assertEqual(first, second)
        self.assertFalse(any(str(c[0]) == "compile" for c in calls))
        old = (self.root / "first/base/bundle.json").read_bytes()
        (self.root / "fixtures/logic_a.luau").write_text('--!strict\nreturn {extra = 2}\n')
        changed = cook(self.root / "changed", self.root / "first")
        for asset in ("0000000000000010", "0000000000000100", "0000000000000101"):
            self.assertNotEqual(first[0]["program_keys"][asset], changed[0]["program_keys"][asset])
        with patch("subprocess.run", side_effect=RuntimeError("strict analysis failed")):
            with self.assertRaises(RuntimeError):
                S2["cook"](self.manifest, self.root / "failed", Path("compile"), Path("analyze"),
                            "pin", "", "contract", "tools")
        self.assertEqual((self.root / "first/base/bundle.json").read_bytes(), old)
        self.assertTrue((self.root / "first/base/maps.json").is_file())


if __name__ == "__main__": unittest.main()
