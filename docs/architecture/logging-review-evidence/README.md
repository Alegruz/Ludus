# Evidence for the September 23, 2026 logging review

This directory contains isolated review probes, not production logger changes.
Baseline source revision: `269b6118a6aeacdc229bf9ba6ee04839bc69f838`.

- `probe.cpp`: demonstrates zero-argument brace handling, direct-call filtering,
  abrupt exit, Error visibility, and eight simultaneous producers.
- `tsan.txt`: full captured data-race report from the instrumented current sources.
- `measure.py`: five compiler-process runs for each include-only input, plus
  deterministic preprocessing sizes.
- `include-measurements.json`: actual samples. Timing was noisy on the shared host
  and overlapped part of a sanitizer compile. It is not a calibrated CI baseline.

The ordinary probe was linked with the current Development logging library after
`./scripts/test linux-clang-development` passed both CTest targets. Its results:

| Mode | Result |
| --- | --- |
| basic | Doubled braces remained; Info direct-call marker survived a Warning threshold |
| abrupt | Warning then 100 ms sleep with a configured 10 ms flush interval, followed by `_Exit`: zero-byte file |
| error | Error then `_Exit`: marker readable, 91 bytes with the original scratch-source path |
| race | Clang 18 TSan reported a shared sink-string data race |

## Reproduce

Run from the repository root. Use a fresh output directory per run. For example:

```bash
./scripts/test linux-clang-development
clang++-18 -std=c++23 -fno-exceptions -O2 -pthread \
  -Imodules/foundation/base/include -Imodules/foundation/logging/include \
  docs/architecture/logging-review-evidence/probe.cpp \
  out/build/linux-clang-development/modules/foundation/logging/libludus_foundation_logging.a \
  -o /tmp/ludus-logging-review-probe
```

Run the resulting executable with `basic`, `abrupt`, or `error` and a fresh
scratch log-directory argument. Read its `.log` files afterward. No test deletes
user files. The `abrupt` and `error` modes deliberately skip C-runtime cleanup.
This tests process-exit behavior, not power-loss persistence.

For a race probe, compile the logger itself with TSan; instrumenting only the
harness is insufficient:

```bash
clang++-18 -std=c++23 -fno-exceptions -O1 -g -fsanitize=thread -pthread \
  -Imodules/foundation/base/include -Imodules/foundation/logging/include \
  -Imodules/foundation/logging/src \
  docs/architecture/logging-review-evidence/probe.cpp \
  modules/foundation/logging/src/logger.cpp \
  modules/foundation/logging/src/formatter.cpp \
  modules/foundation/logging/src/emergency_logger.cpp \
  modules/foundation/logging/src/sinks/console_sink.cpp \
  modules/foundation/logging/src/sinks/debugger_sink.cpp \
  modules/foundation/logging/src/sinks/file_sink.cpp \
  -o /tmp/ludus-logging-review-probe-tsan
TSAN_OPTIONS=halt_on_error=1 /tmp/ludus-logging-review-probe-tsan race /tmp/ludus-logging-review-race-output
```

The captured report used the same source copied to `/tmp/ludus-logging-review`.
Its harness line numbers refer to that original unformatted scratch file; logger
source locations remain the reviewed repository locations. A second run also
reproduced the race. Default TSan nonzero exit on detection is expected.

Run `python3 docs/architecture/logging-review-evidence/measure.py` for include
measurements. The script derives the repository location, uses its configured
Clang binary, and writes temporary source inputs/results to a newly created
scratch directory. Record machine load and toolchain before comparisons. These
measurements do not include call-site instantiation or link/code-size costs;
those are specified in the report's acceptance plan.
