"""Measure existing logging headers without changing engine sources."""

import json
from pathlib import Path
import statistics
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[3]
output = Path(tempfile.mkdtemp(prefix="ludus-log-includes-"))
print("Scratch output:", output)
compiler = [
    str(root / "out/host-tools/bin/clang++"),
    "-std=c++23",
    "-fno-exceptions",
    "-stdlib=libstdc++",
    "-I" + str(root / "modules/foundation/base/include"),
    "-I" + str(root / "modules/foundation/logging/include"),
]
rows = []
for name, header in [
    ("empty", None),
    ("category", "category.hpp"),
    ("config", "config.hpp"),
    ("log", "log.hpp"),
]:
    source = output / (name + ".cpp")
    source.write_text(
        "" if header is None else f"#include <ludus/foundation/logging/{header}>\n"
    )
    elapsed = []
    for _ in range(5):
        start = time.perf_counter()
        subprocess.run(
            compiler + ["-fsyntax-only", str(source)], check=True, capture_output=True
        )
        elapsed.append((time.perf_counter() - start) * 1000)
    preprocessed = subprocess.run(
        compiler + ["-E", str(source)], check=True, capture_output=True
    ).stdout
    rows.append(
        {
            "header": name,
            "median_ms": round(statistics.median(elapsed), 2),
            "runs_ms": [round(value, 2) for value in elapsed],
            "preprocessed_bytes": len(preprocessed),
            "preprocessed_lines": preprocessed.count(b"\n"),
        }
    )
result = json.dumps(rows, indent=2)
(output / "include-measurements.json").write_text(result + "\n")
print(result)
