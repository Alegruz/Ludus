"""Regression tests for tools/check_naming.py."""

import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("check_naming", ROOT / "tools" / "check_naming.py")
check_naming = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(check_naming)


class CheckNamingTests(unittest.TestCase):
    def findings(self, source: str) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "a.cpp"
            path.write_text(source)
            return [finding.split("] ")[0].split("[")[1] for finding in check_naming.check_file(root, path)]

    def test_nullable_members_locals_and_move_parameters(self) -> None:
        self.assertEqual(self.findings("struct A { Thing* mValue{}; }; Thing* value = nullptr;"), ["N4", "N4"])
        self.assertEqual(self.findings("void Move(Thing&& inoutOther);"), ["N2"])
        self.assertEqual(self.findings("void Move(Thing&& other); Thing* valueOrNull = nullptr;"), [])

    def test_reports_each_rule(self) -> None:
        source = "bool F(Foo& foo, int* p = nullptr, bool ok);\nvoid G() { if (!x) {} }\n"
        self.assertEqual(sorted(self.findings(source)), ["N1", "N2", "N3", "N4"])

    def test_accepts_house_style(self) -> None:
        source = (
            "bool F(Foo& inoutFoo, const Bar& bar, Baz& outBaz, int* pOrNull = nullptr, bool isOk);\n"
            "void G() { if (x == false && a != b) {} bool hasItems = true; for (Q& q : r) {} int z = a & b; }\n"
            "// !comment and Foo& foo, in a comment\nconst char* s = \"!text\";\n"
        )
        self.assertEqual(self.findings(source), [])


if __name__ == "__main__":
    unittest.main()
