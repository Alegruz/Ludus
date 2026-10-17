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

    def test_nullable_variables_and_move_parameters(self):
        for source in ["Thing* value = nullptr;", "struct A { Thing* mValue{}; };", "Thing* const value = nullptr;",
                       "void Move(Thing&& inoutOther);", "void Move(Thing&& outOther);"]:
            self.assertTrue(self.diagnose(source), source)
        for source in ["Thing* valueOrNull = nullptr;", "void Move(Thing&& other);",
                       "void Read(const Thing& input);", "Thing* value = GetRequired();"]:
            self.assertFalse(self.diagnose(source), source)
        self.assertFalse(self.diagnose('// Thing* value = nullptr;\nconst char* text = "Thing&& inoutOther";'))

    def test_pointee_assignment_does_not_make_the_pointer_nullable(self):
        self.assertFalse(self.diagnose("void Clear(Thing** required) { *required = nullptr; }"))

    def test_shader_reuses_owned_pi_constant(self):
        self.assertTrue(self.diagnose("float Shade() { return 1 / 3.141592653589793; }", ".slang"))
        self.assertFalse(self.diagnose("float Shade() { return 1 / PI; }", ".slang"))
        self.assertFalse(violations("modules/foundation/math/shaders/math.slang",
                                   "static const float PI = 3.141592653589793;", {1}))

    def test_shader_else_if_is_control_flow(self):
        self.assertFalse(
            self.diagnose("void Shade() { if (x) {} else if (y) {} }", ".slang")
        )

    def test_one_variable_per_declaration(self):
        for suffix in [".cpp", ".slang"]:
            for text in [
                "usize first = 0, second = 0;",
                "uint first, second;",
                "struct Pair { float3 PositionWorld, NormalWorld; };",
                "const uint32 first[] = {1, 2}, second[] = {3, 4};",
                "void Run() { usize first = F(1, 2),\n second = 0; }",
                "for (uint first = 0, second = 1; first < 2; ++first) {}",
                "uint first = 0; uint second = 1, third = 2;",
                "Handle* first = nullptr, second = nullptr;",
                "namespace detail\n{\nuint first, second;\n}",
            ]:
                with self.subTest(text=text, suffix=suffix):
                    self.assertTrue(self.diagnose(text, suffix))

    def test_expression_commas_are_not_declarations(self):
        for text in [
            "uint first = F(1, 2); uint second = 0;",
            "const Pair value = {1, 2};",
            "Pair<int, int> value = Make<Pair<int, int>>(1, 2);",
            "void Run(uint first, uint second) { F(first, second); }",
            "for (uint i = 0; i < 2; ++i, Update()) {}",
            "uint value = [] { return F(1, 2); }();",
            '// uint first, second;\nconst char* text = "uint first, second;";',
            "namespace detail\n{\nvoid F();\n}\ntemplate <typename A, usize B>\nclass C\n{\n};",
            "namespace a::b\n{\n}\ntemplate <typename A, typename B>\nusing Pair = Make<A, B>;",
        ]:
            with self.subTest(text=text):
                self.assertFalse(self.diagnose(text))

    def test_builtin_shader_recipes_have_one_owner(self):
        self.assertTrue(
            self.diagnose(
                'ludus_compile_shader(TARGET app NAME renderer_lit RASTER SOURCE "lit.slang")',
                ".cmake",
            )
        )
        self.assertFalse(
            self.diagnose(
                "ludus_compile_renderer_shaders(TARGET app SHADERS renderer_lit)",
                ".cmake",
            )
        )
        self.assertFalse(
            self.diagnose(
                'ludus_compile_shader(TARGET app NAME custom SOURCE "custom.slang")',
                ".cmake",
            )
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

    def test_explicit_boolean_comparisons(self):
        for text in (
            "if (!IsReady()) {}",
            "bool hasFailed = !ready;",
            "if (not IsReady()) {}",
        ):
            with self.subTest(text=text):
                self.assertTrue(self.diagnose(text))
        self.assertFalse(self.diagnose("if (IsReady() == false || status != Ready) {}"))
        self.assertFalse(self.diagnose("bool operator!=(const Value& other) const;"))

    def test_negation_ignores_literals_comments_and_preprocessor(self):
        self.assertFalse(
            self.diagnose(
                '// !ready and not ready\nconst char* message = R"(!ready, not ready)";\n#if !defined(TEST)\n#endif\n'
            )
        )

    def test_negation_ratchet_reports_only_touched_code(self):
        before = "if (!legacy) {}\n"
        after = before + "if (!current) {}\n"
        failures = violations("modules/a.cpp", after, added_lines(before, after))
        self.assertEqual([line for line, _ in failures], [2])

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

    def test_camel_case_rejects_underscores_after_correct_initial_case(self):
        for text in [
            "struct Sample_type { float Value; };",
            "struct Sample { float Value_count; };",
            "float Evaluate_brdf(float value) { return value; }",
            "float Evaluate(float incident_value) { return incident_value; }",
            "void Evaluate(out float outValue_count) { outValue_count = 0; }",
        ]:
            with self.subTest(text=text):
                self.assertTrue(self.diagnose(text, ".slang"))

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
