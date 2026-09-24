"""Compile semantic failures and inspect real optimized objects/include closure."""

import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build_contract"))
from verify_compile_policy import compiler_options


def main():
    build = Path(sys.argv[1]).resolve()
    enabled = int(sys.argv[2])
    entry = next(e for e in json.loads((build / "compile_commands.json").read_text())
                 if e["file"].endswith("base/src/version.cpp"))
    options = compiler_options(entry)
    header = "#include <ludus/foundation/base/assert.hpp>\n"

    def syntax(body):
        return subprocess.run(options + ["-x", "c++", "-fsyntax-only", "-"],
                              input=header + body, text=True, capture_output=True, cwd=entry["directory"])

    unknown = syntax("void probe() { LUDUS_ASSERT(missing_condition(), missing_message()); }\n")
    assert (unknown.returncode != 0) == bool(enabled), unknown.stderr
    if enabled:
        assert "undeclared identifier" in unknown.stderr, unknown.stderr
    no_reason = syntax("void probe() { LUDUS_FATAL(); }\n")
    assert no_reason.returncode != 0 and "expected expression" in no_reason.stderr, no_reason.stderr
    for macro in ("LUDUS_ASSERT", "LUDUS_REQUIRE"):
        result = syntax(f"constexpr bool probe([[maybe_unused]] bool ok) {{ {macro}(ok); return true; }}\nstatic_assert(probe(false));\n")
        should_fail = macro == "LUDUS_REQUIRE" or enabled
        assert (result.returncode != 0) == bool(should_fail), result.stderr
        if should_fail:
            assert "constant expression" in result.stderr, result.stderr

    includes = subprocess.run(options + ["-x", "c++", "-H", "-E", "-"], input=header,
                              text=True, capture_output=True, cwd=entry["directory"], check=True)
    forbidden = {"format", "filesystem", "chrono", "atomic", "source_location", "iostream", "string",
                 "string_view", "vector", "mutex", "log.hpp", "unistd.h", "pthread.h", "fcntl.h"}
    included = {Path(line.lstrip(". ")).name for line in includes.stderr.splitlines() if line.startswith(".")}
    assert not (included & forbidden), included & forbidden

    plain_header = header
    header = "#include <ludus/foundation/base/assert_format.hpp>\nusing namespace ludus::foundation;\nusing namespace ludus::foundation::diagnostics;\n"
    for expression in ("int8{1}", "uint8{1}", "int16{1}", "uint16{1}", "int32{1}", "uint32{1}",
                       "int64{1}", "uint64{1}", "usize{1}", "isize{1}", "float32{1}", "float64{1}",
                       "true", '"text"', "DiagnosticText{nullptr, 0}", "DiagnosticAddress(nullptr)", "DiagnosticCString(nullptr)"):
        result = syntax(f'void probe() {{ (void)LUDUS_CHECK_F(false, "{{}}", {expression}); }}')
        assert result.returncode == 0, (expression, result.stderr)
    for declaration, expression in (("int value;", "&value"), ("void* value = nullptr;", "value"),
                                    ("const char* value = nullptr;", "value"), ("", "nullptr"),
                                    ("struct Object {} value;", "value"),
                                    ("struct Object { operator bool() const; } value;", "value"),
                                    ("enum class E { Value };", "E::Value"),
                                    ("void function();", "&function"),
                                    ("struct Object { int Member; };", "&Object::Member")):
        result = syntax(f'void probe() {{ {declaration} (void)LUDUS_CHECK_F(false, "{{}}", {expression}); }}')
        assert result.returncode != 0 and "deleted" in result.stderr, (expression, result.stderr)
    for count in (8, 9):
        result = syntax('void probe() { (void)LUDUS_CHECK_F(false, "' + '{} ' * count + '", ' + ','.join(['1'] * count) + '); }')
        assert (result.returncode == 0) == (count == 8), result.stderr
        if count == 9:
            assert "at most eight" in result.stderr, result.stderr
    result = syntax('void probe() { const char* format = "{}"; (void)LUDUS_CHECK_F(false, format, 1); }')
    assert result.returncode != 0, "Runtime format pointer accepted"
    result = syntax('void probe() { LUDUS_ASSERT_F(missing_condition(), missing_format, missing_argument); }')
    assert (result.returncode != 0) == bool(enabled), result.stderr
    format_includes = subprocess.run(options + ["-x", "c++", "-H", "-E", "-"], input=header,
                                     text=True, capture_output=True, cwd=entry["directory"], check=True)
    included = {Path(line.lstrip(". ")).name for line in format_includes.stderr.splitlines() if line.startswith(".")}
    assert not (included & forbidden), included & forbidden
    header = plain_header

    output = build / "tests/assertion-codegen"
    output.mkdir(parents=True, exist_ok=True)
    (output / "includes.txt").write_text(includes.stderr)
    # Machine-code budget is an unsanitized optimized baseline. Semantic/death
    # tests still link and execute the fully instrumented production library.
    codegen_options = options + ["-fno-sanitize=all"]
    for kind in ("ASSERT", "REQUIRE", "CHECK", "ASSERT_F", "REQUIRE_F", "CHECK_F"):
        body = (header if not kind.endswith("_F") else header.replace("assert.hpp", "assert_format.hpp")) + f'''extern "C" int Probe{kind}(int CONDITION_7ead) {{
    ''' + ("return " if kind.startswith("CHECK") else "") + f'''LUDUS_{kind}(CONDITION_7ead > 0, "MESSAGE_7ead");
    ''' + ("" if kind.startswith("CHECK") else "return CONDITION_7ead + 1;") + "\n}\n"
        if kind.endswith("_F"):
            body = body.replace('"MESSAGE_7ead"', '"MESSAGE_7ead {}", CONDITION_7ead')
        for option, suffix in (("-S", ".s"), ("-c", ".o")):
            subprocess.run(codegen_options + ["-O2", "-g0", "-x", "c++", option, "-", "-o", str(output / (kind + suffix))],
                           input=body, text=True, cwd=entry["directory"], check=True)
        if kind in ("ASSERT", "ASSERT_F") and not enabled:
            linked = output / (kind + "-lto")
            subprocess.run(codegen_options + ["-O2", "-g0", "-flto", "-fuse-ld=lld", "-x", "c++", "-",
                           "-o", str(linked)], input=body + f"int main(int argc, char**) {{ return Probe{kind}(argc); }}",
                           text=True, cwd=entry["directory"], check=True)
            assert b"MESSAGE_7ead" not in linked.read_bytes() and b"CONDITION_7ead" not in linked.read_bytes()
        assembly = (output / (kind + ".s")).read_text()
        binary = (output / (kind + ".o")).read_bytes()
        # ASSERT/ASSERT_F use the resumable BeginAssert entry; CHECK/CHECK_F use
        # BeginCheck; REQUIRE/FATAL keep the terminal BeginFatal.
        expected_entry = ("BeginAssert" if kind.startswith("ASSERT")
                          else "BeginCheck" if kind.startswith("CHECK")
                          else "BeginFatal")
        if kind.startswith("ASSERT") and not enabled:
            assert b"MESSAGE_7ead" not in binary and b"CONDITION_7ead" not in binary
            assert "BeginAssert" not in assembly and "FinishAssert" not in assembly
        else:
            assert b"MESSAGE_7ead" in binary and b"CONDITION_7ead > 0" in binary
            assert expected_entry in assembly
        # No TLS/atomic/allocator/debugger machinery at the call sites.
        for token in ("%fs:", "__tls", "lock\t", "malloc", "QueryDebugger", "clock_gettime"):
            assert token not in assembly, (kind, token)
    print(f"Verified constexpr/elision/include closure and optimized objects; artifacts: {output}")


if __name__ == "__main__":
    main()
