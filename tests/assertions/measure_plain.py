"""Manual plain-header benchmark; not a per-commit unit test. Clang 18, no cache.

Usage: python3 tests/assertions/measure_plain.py BUILD_DIR OUTPUT_DIR
Outputs five-run medians, deviations, RSS and section sizes for macro/handwritten
equivalents. Interleave paired cases to reduce ordering bias on noisy hosts.
Run on an otherwise idle host. Includes the closed formatter and tagged-array baseline.
"""

import json
from pathlib import Path
import statistics
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build_contract"))
from verify_compile_policy import compiler_options


def main():
    build, output = (Path(p).resolve() for p in sys.argv[1:3])
    output.mkdir(parents=True, exist_ok=True)
    entry = next(e for e in json.loads((build / "compile_commands.json").read_text())
                 if e["file"].endswith("base/src/version.cpp"))
    args = [arg for arg in compiler_options(entry) if not arg.startswith(("-ftime-trace", "-O", "-g"))]
    args += ["-fno-sanitize=all", "-O0", "-g0", "-ftime-trace", "-c"]
    source, obj, rss = output / "sample.cpp", output / "sample.o", output / "rss.txt"
    results = {}

    def measure(content):
        source.write_text(content)
        subprocess.run(["/usr/bin/time", "-f", "%M", "-o", str(rss), *args,
                        str(source), "-o", str(obj)], check=True, cwd=entry["directory"], capture_output=True)
        events = json.loads(obj.with_suffix(".json").read_text())["traceEvents"]
        times = {e["name"]: e["dur"] / 1000 for e in events if e.get("name", "").startswith("Total ")}
        counts = {e["name"]: e.get("args", {}).get("count", 0) for e in events}
        sections = subprocess.run(["size", "-A", str(obj)], text=True, capture_output=True, check=True).stdout
        section_sizes = {line.split()[0]: int(line.split()[1]) for line in sections.splitlines()
                         if line.startswith(".")}
        return {"frontend_ms": times["Total Frontend"], "backend_ms": times["Total Backend"],
                "rss_kib": int(rss.read_text()), "object_bytes": obj.stat().st_size,
                "text_bytes": sum(v for k, v in section_sizes.items() if k.startswith(".text")),
                "rodata_bytes": sum(v for k, v in section_sizes.items() if k.startswith(".rodata")),
                "class_instantiations": counts.get("Total InstantiateClass", 0),
                "function_instantiations": counts.get("Total InstantiateFunction", 0)}

    def measure_pair(samples):
        observations = {label: [] for label, _ in samples}
        for run in range(5):
            for label, content in samples if run % 2 == 0 else reversed(samples):
                observations[label].append(measure(content))
        for label, runs in observations.items():
            median = {key: statistics.median(r[key] for r in runs) for key in runs[0]}
            deviation = {key: statistics.pstdev(r[key] for r in runs) for key in runs[0]}
            results[label] = {"median": median, "standard_deviation": deviation, "runs": runs}
            print(label, median, flush=True)

    include = "#include <ludus/foundation/base/assert.hpp>\n"
    measure_pair([("empty", "\n"), ("include-only", include)])
    for count in (100, 1000, 10000):
        for message in (False, True):
            bodies = {"macro": [], "handwritten": []}
            for i in range(count):
                condition = f"value != {i}"
                argument = '"plain-message"' if message else ""
                bodies["macro"].append(f"LUDUS_ASSERT({condition}{', ' + argument if message else ''});")
                bodies["handwritten"].append(
                    f"if (!static_cast<bool>({condition})) [[unlikely]] {{ "
                    "::ludus::foundation::diagnostics::detail::BeginFatal("
                    "::ludus::foundation::diagnostics::FailureKind::Assert, "
                    "::ludus::foundation::diagnostics::AssertionSite{"
                    f'__FILE__, __func__, "{condition}", static_cast<::ludus::foundation::uint32>(__LINE__)}}); '
                    f"::ludus::foundation::diagnostics::detail::FinishFatal({argument}); }}")
            measure_pair([(f"{count}-{'literal' if message else 'plain'}-{style}",
                           include + "void Probe(int value) {\n" + "\n".join(body) + "\n}\n")
                          for style, body in bodies.items()])
    format_include = include.replace("assert.hpp", "assert_format.hpp")
    logging_include = '#include "' + str(Path(__file__).resolve().parents[2] / "modules/foundation/logging/include/ludus/foundation/logging/log.hpp") + '"\n'
    args += ["-I" + str(Path(__file__).resolve().parents[2] / "modules/foundation/logging/include")]
    measure_pair([("format-include-only", format_include), ("logging-include-only", logging_include)])
    for count in (100, 1000, 10000):
        for diverse in (False, True):
            bodies = {"macro": [], "handwritten": []}
            for i in range(count):
                kind = ("int32", "uint64", "float64", "bool")[i % 4] if diverse else "int32"
                value = f"static_cast<::ludus::foundation::{kind}>(value)" if kind != "bool" else "static_cast<bool>(value)"
                condition = f"value != {i}"
                bodies["macro"].append(f'LUDUS_ASSERT_F({condition}, "value={{}}", {value});')
                bodies["handwritten"].append(
                    f"if (!static_cast<bool>({condition})) [[unlikely]] {{ "
                    "using namespace ::ludus::foundation::diagnostics; "
                    f'detail::BeginFatal(FailureKind::Assert, AssertionSite{{__FILE__, __func__, "{condition}", '
                    'static_cast<::ludus::foundation::uint32>(__LINE__)}); '
                    f'const detail::DiagnosticArg packed[] = {{detail::MakeDiagnosticArg({value})}}; '
                    'detail::FinishFatalArgs({"value={}", sizeof("value={}")}, packed, 1); }')
            measure_pair([(f"{count}-{'diverse' if diverse else 'repeated'}-{style}",
                           format_include + "void Probe(int value) {\n" + "\n".join(body) + "\n}\n")
                          for style, body in bodies.items()])
    report = output / "measurements.json"
    report.write_text(json.dumps(results, indent=2) + "\n")
    print(f"Recorded five-run measurements: {report}")


if __name__ == "__main__":
    main()
