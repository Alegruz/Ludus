"""Regressions for the manifest boundary and stale/modified cook admission."""
import copy
import json
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
GEN = runpy.run_path(str(ROOT / "tools/script-cook/generate.py"), run_name="generator")
TOOL = runpy.run_path(str(ROOT / "scripts/script-interaction"), run_name="tool")
DATA = json.loads((ROOT / "tools/script-interaction/interaction.json").read_text())


class ManifestTests(unittest.TestCase):
    def load(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(text)
            return GEN["load"](path)

    def test_duplicate_keys_ids_and_unknown_keys_rejected(self):
        with self.assertRaisesRegex(ValueError, "duplicate JSON key"):
            self.load('{"version":1,"version":1}')
        for mutation in (
            lambda d: d.update(typo=1),
            lambda d: d["schemas"]["State"].update(id=100),
            lambda d: d["schemas"]["State"]["fields"][1].update(id=1),
            lambda d: d["operation"].update(id=100),
        ):
            data = copy.deepcopy(DATA)
            mutation(data)
            with self.assertRaises(ValueError):
                self.load(json.dumps(data))

    def test_types_bounds_and_injection_rejected(self):
        for key, value in (("name", 'State";system("bad")'), ("type", "uint64"),
                           ("default", True), ("min", -1), ("max", 1 << 32)):
            data = copy.deepcopy(DATA)
            data["schemas"]["State"]["fields"][0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.load(json.dumps(data))
        for key, value in (("phase", "Intent"), ("capability", "Everything"), ("effect", "DirectMutation")):
            data = copy.deepcopy(DATA)
            data["operation"][key] = value
            with self.assertRaises(ValueError):
                self.load(json.dumps(data))

    def test_generation_changes_definitions_bounds_and_operation_together(self):
        original = self.load(json.dumps(DATA))
        before = GEN["generate"](original, "a" * 64)
        changed = copy.deepcopy(original)
        changed["schemas"]["State"]["fields"][0]["max"] = 7
        changed["operation"]["id"] = 201
        after = GEN["generate"](changed, "b" * 64)
        self.assertNotEqual(before["contract.h"], after["contract.h"])
        self.assertIn("OPERATION_ID = 201", after["contract.h"])
        self.assertIn("value.Interactions <= 7", after["contract.h"])
        self.assertIn("Number(state, 3, { .Min = 0, .Max = 7 })", after["bindings.cpp"])
        self.assertIn("RequestDoorOpen", after["contract.d.luau"])
        self.assertIn("OPERATION_ID", after["bindings.cpp"])
        self.assertNotIn("RequestDoorOpen_ID", after["bindings.cpp"])

    def test_stale_and_modified_generated_inputs_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            cooked = work / "cooked"
            cooked.mkdir()
            (work / "host.json").write_text("{}")
            (cooked / "contract.h").write_text("checked")
            evidence = {"inputs": {"fixture": "current"}, "host": {},
                        "outputs": {"contract.h": TOOL["sha"](b"checked")}}
            (cooked / "evidence.json").write_text(json.dumps(evidence))
            variables = TOOL["verify"].__globals__
            with patch.dict(variables, WORK=work, inputs=lambda: {"fixture": "current"}, check_host=lambda: None):
                TOOL["verify"]()
                (cooked / "contract.h").write_text("modified")
                with self.assertRaisesRegex(RuntimeError, "Modified S1"):
                    TOOL["verify"]()
            with patch.dict(variables, WORK=work, inputs=lambda: {"fixture": "changed"}, check_host=lambda: None):
                with self.assertRaisesRegex(RuntimeError, "Stale S1"):
                    TOOL["verify"]()

    def test_references_cannot_be_persistent_state_in_this_slice(self):
        data = copy.deepcopy(DATA)
        data["schemas"]["State"]["fields"][0] = {"id": 1, "name": "Handle", "type": "EntityRef"}
        with self.assertRaisesRegex(ValueError, "references only supported in events"):
            self.load(json.dumps(data))


if __name__ == "__main__":
    unittest.main()
