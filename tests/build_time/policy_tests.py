import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("build_time_policy", ROOT / "tools/check_build_time_includes.py")
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


class PolicyTests(unittest.TestCase):
    def test_direct_and_transitive_boundary_policy(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            files = {
                "modules/foo/include/public.hpp": "#include <filesystem>\n",
                "modules/foo/src/private.cpp": '#include <format>\nauto s = std::format("{}", 1);\n',
                "tests/fixture.cpp": 'auto s = std::vformat("{}", args);\n',
                "modules/foo/src/allowed.cpp": '#include <concepts>\n// std::format\nconst char* note = "std::format // <format>";\n',
                "third_party/fixture.cpp": "#include <format>\n",
            }
            for name, text in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            problems = policy.violations(root)
            self.assertEqual(len(problems), 3, problems)

    def test_repository(self):
        self.assertEqual(policy.violations(ROOT), [])


if __name__ == "__main__":
    unittest.main()
