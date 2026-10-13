"""Regressions for changed-source review rules and literal safety."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from source_policy import added_lines, check, violations


class SourcePolicyTests(unittest.TestCase):
    def diagnose(self, text, suffix=".cpp"):
        return violations(
            "modules/example/source" + suffix, text, set(range(1, text.count("\n") + 2))
        )

    def test_explicit_values_and_unspellable_lambdas(self):
        self.assertFalse(
            self.diagnose(
                "const Status status = Poll();\nauto callback = [&] { return status; };\n"
            )
        )
        self.assertTrue(self.diagnose("const auto status = Poll();\n"))
        self.assertTrue(self.diagnose("for (const auto& item : items) {}\n"))

    def test_direct_initializers_and_structured_bindings(self):
        self.assertTrue(self.diagnose("auto status(Poll());"))
        self.assertTrue(self.diagnose("auto const& status{Poll()};"))
        self.assertTrue(self.diagnose("auto [first, second] = Pair();"))
        self.assertFalse(self.diagnose("auto Function() -> Status { return Poll(); }"))

    def test_reviewable_iterator_exception(self):
        self.assertFalse(
            self.diagnose(
                "auto iterator = items.begin(); // ludus-style: inferred-type: iterator implementation-dependent iterator spelling\n"
            )
        )
        self.assertTrue(
            self.diagnose(
                "auto status = Poll(); // ludus-style: inferred-type: convenient\n"
            )
        )

    def test_comments_literals_and_preprocessor_are_opaque(self):
        self.assertFalse(
            self.diagnose(
                '// auto status = Poll();\nconst char* message = R"(auto status = Poll();)";\n#define AUTO_EXAMPLE auto status = Poll()\n'
            )
        )

    def test_shader_members_functions_and_spaces(self):
        good = "struct Hit { float3 PositionWorld; float DistanceWorld; };\nfloat3 NormalizeOrZero(float3 value) { return value; }\n"
        self.assertFalse(self.diagnose(good, ".slang"))
        self.assertTrue(self.diagnose("struct Hit { float3 position; };", ".slang"))
        self.assertTrue(self.diagnose("struct Hit { float3 Position; };", ".slang"))
        self.assertTrue(
            self.diagnose("float normalize(float value) { return value; }", ".slang")
        )

    def test_shader_types_and_parameters(self):
        self.assertTrue(self.diagnose("struct sample { float Value; };", ".slang"))
        self.assertTrue(
            self.diagnose("float Evaluate(float Value) { return Value; }", ".slang")
        )
        self.assertFalse(
            self.diagnose("float Evaluate(float value) { return value; }", ".slang")
        )

    def test_shader_outputs_and_entry_points(self):
        self.assertFalse(
            self.diagnose(
                "void MultiplyWide(uint a, out uint outLo) { outLo = a; }", ".slang"
            )
        )
        self.assertTrue(
            self.diagnose(
                "void MultiplyWide(uint a, out uint lo) { lo = a; }", ".slang"
            )
        )
        self.assertTrue(
            self.diagnose(
                "[numthreads(64,1,1)] void computeMain(uint3 id : SV_DispatchThreadID) {}",
                ".slang",
            )
        )

    def test_annotations_are_owned_by_rhi(self):
        text = "[[vk::binding(0,0)]] StructuredBuffer<uint4> scene;"
        self.assertTrue(self.diagnose(text, ".slang"))
        self.assertFalse(
            violations("modules/graphics/rhi/shaders/raster_bindings.slang", text, {1})
        )

    def test_unchanged_debt_is_not_a_bypass_for_new_lines(self):
        before = "auto old = Poll();\n"
        after = before + "auto added = Poll();\n"
        self.assertEqual(added_lines(before, after), {2})
        failures = violations("modules/a.cpp", after, added_lines(before, after))
        self.assertEqual([line for line, _ in failures], [2])

    def test_git_ratchet_checks_tracked_and_untracked_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            def git(*arguments):
                return subprocess.run(
                    ["git", *arguments],
                    cwd=root,
                    check=True,
                    text=True,
                    capture_output=True,
                ).stdout

            git("init", "-q")
            git("config", "user.name", "Policy test")
            git("config", "user.email", "policy@example.invalid")
            (root / "modules").mkdir()
            source = root / "modules/a.cpp"
            source.write_text("auto old = Poll();\n")
            git("add", ".")
            git("commit", "-qm", "fixture")
            base = git("rev-parse", "HEAD").strip()
            self.assertFalse(check(root, base))
            source.write_text("auto old = Poll();\nauto added = Poll();\n")
            (root / "modules/new.slang").write_text(
                "float helper(float value) { return value; }"
            )
            self.assertEqual(len(check(root, base)), 2)

    def test_pr_head_excludes_unrelated_synthetic_merge_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            def git(*arguments):
                return subprocess.run(
                    ["git", *arguments],
                    cwd=root,
                    check=True,
                    text=True,
                    capture_output=True,
                ).stdout.strip()

            git("init", "-q", "-b", "main")
            git("config", "user.name", "Policy test")
            git("config", "user.email", "policy@example.invalid")
            (root / "modules").mkdir()
            source = root / "modules/a.cpp"
            source.write_text("auto legacy = Poll();\n")
            git("add", ".")
            git("commit", "-qm", "base")
            base = git("rev-parse", "HEAD")
            git("checkout", "-qb", "feature")
            source.write_text("auto legacy = Poll();\nStatus current = Poll();\n")
            git("add", ".")
            git("commit", "-qm", "feature")
            head = git("rev-parse", "HEAD")
            git("checkout", "-q", "main")
            (root / "modules/other.cpp").write_text("auto unrelated = Poll();\n")
            git("add", ".")
            git("commit", "-qm", "unrelated main change")
            git("merge", "-q", "--no-edit", "feature")
            self.assertTrue(check(root, base))
            self.assertFalse(check(root, base, head=head))
            # Head-specific checking still rejects actual PR violations.
            self.assertTrue(check(root, head, head=git("rev-parse", "HEAD")))


if __name__ == "__main__":
    unittest.main()
