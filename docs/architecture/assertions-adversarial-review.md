# Assertion adversarial review

## Pre-edit audit (2026-09-23)

Reviewed the complete assertion contract, reconciliation plan, repository rules,
ADRs 0003–0005, implementation, tests, build/install graph, and existing diff.
The M0/M1 files were already uncommitted; this review builds on that work.

| Requirement | Pre-edit status | Evidence / action |
| --- | --- | --- |
| Tiny plain header; Base has no upward dependency | Satisfied by source inspection; existing header/codegen tests | Keep public include closure unchanged. |
| Passing path has no runtime coordination | Implemented, previously codegen-tested | Repeat generated-code checks after formatting addition. |
| Single evaluation, contextual bool, disabled token erasure, statement safety | Implemented and covered by existing fixtures | Extend these fixtures to formatted macros. |
| Begin precedes diagnostic expressions | Satisfied by macro inspection and native packet test | Preserve for every formatted form. |
| Explicit variant policy and SDK agreement | Implemented and previously verified across four flavors | Repeat install and configuration tests. Profile is already corrected. |
| Nonreturning fatal after debugger continuation | Implemented and covered by fake/native death tests | Preserve real abort/_Exit; no throwing test replacement. |
| TLS recursion / nonwaiting owner / CHECK budget | Implemented, insufficient concurrency coverage | Add synchronized contention, application-lock, packet-reader tests. |
| Fatal output cannot prevent fatal action | **Violated** | Full blocking stderr pipe reproducibly hangs fatal allocation child; watchdog killed it after one second. Replace assertion delivery with bounded-attempt nonblocking socket sends. |
| Bounded reports preserve all identity fields | **Violated by inspection** | Unbounded share of report space for file/expression can erase function/status/message labels. Add per-field output budgets. |
| Fixed owned fatal packet, acquire/release stages | Minimal/complete implemented, insufficient verification | Add immutable delivery stage and reader tests; CHECK never owns packet. |
| Closed formatter and opt-in header | Intentionally deferred in M1, now approved M3 | Implement finite scalar/text/address tags and non-template parser; no generic formatting. |
| Shared emergency byte boundary | Intentionally deferred in M1, now approved M4 | Logging may use Base writer; assertions never enter Logging. |
| Actual artifact ID / stack collector | Intentionally deferred | Only git revision exists; report artifact/stack unavailable. No symbolizer/uploader. |
| Windows/macOS / LLP64 | Intentionally deferred | Configure explicitly rejects unsupported backend; Linux/Clang only. |
| Null UniquePtr destruction | Stale TODO | Remove misleading empty TODO only; null destruction is valid. |

## Selected hardening boundary

Assertions use a process-lifetime, duplicated connected Unix datagram socket,
configured once during healthy startup before worker launch. Sends use
`MSG_DONTWAIT | MSG_NOSIGNAL` with bounded EINTR retries. No transport means
external delivery is skipped; fatal identity remains committed in the packet.
This is the contract's explicit permission to skip unsafe external writes, not
a guarantee of external visibility, durability, or a wall-clock bound under OS
scheduling. A full or broken endpoint cannot cause a blocking write or SIGPIPE.
No collector implementation is added. Ordinary Logging retains the explicitly
potentially blocking stderr fallback at the shared byte boundary.

Keep 512/2048 packet capacities, 64 detailed CHECK reports, eight format arguments,
1024-byte text scans, and 2048-byte format bound. Field quotas reserve identity
space rather than growing buffers. No condition, action, ownership, or budget
semantics are weakened for tests.

Validation results and remaining limitations will be appended after implementation.

## Implemented changes

- `diagnostic_output.hpp/.cpp`: startup-only native socket configuration, owned
  duplicated descriptor, bounded nonblocking sends, explicit delivery status.
  A configured failed channel does not fall back to blocking stderr. Logging's
  final byte writer now shares this Base implementation; Logging keeps its own
  record construction and ordinary blocking stderr fallback.
- `assert.cpp`: unchanged owner/TLS/budget/action protocol; per-field quotas and
  separate delivery publication. Complete reports retain policy version,
  flavor, kind, native thread, file/line, function, expression, capture status,
  and a message field. Git revision is identified as revision, not artifact ID.
- `diagnostic_record.hpp`: immutable minimal, complete, and delivery stages;
  each stage requires its own acquire-ready read. Reading delivery fields merely
  because `MinimalReady` is true is invalid. No packet reset or CHECK reuse.
- `assert_format.hpp`: closed integer/float/bool/text/address packing, eight
  arguments, fixed-array formats, deleted unsupported-type conversion. Public
  plain/format headers are 96/189 physical lines at this review.
- `diagnostic_format.cpp`: runtime grammar validation, escaped fixed-buffer
  output, indexed malformed-format fallback, invalid internal tag fallback, and implementation-only `to_chars`.
  Float32 normalizes to float64; text is bytes, not a Unicode conversion service.
- Private `diagnostic_finish.hpp` keeps the dependency one-way: formatted calls
  render into a 2048-byte stack buffer, then copy escaped bytes into the report.
  Plain-only binaries need not link the parser/converter. Formatted CHECK uses
  roughly two report buffers on the failure stack, not the passing path.
- Removed only the misleading null-UniquePtr destruction TODO. No external
  input errors were converted into invariants; no migration quota was invented.

Field budgets are **encoded output bytes**: complete file/expression 480 each,
function 128, revision 64; minimal file/expression 160 each. This leaves room
inside the original capacities for labels, IDs, status, and message. Escapes
are appended atomically; global truncation remains explicit. Optional message
bytes cannot consume required identity fields because identity is built first.

## Design clarifications / deviations

1. The example format adapter now passes the extent **including** the final NUL.
   This permits runtime rejection of unterminated nonliteral fixed arrays. The
   2048-byte format inspection cap includes this terminator. No runtime format
   pointer overload was introduced.
2. Tagged arguments retain a truncation bit. A short unterminated `char[N]`
   cannot safely masquerade as a 1025-byte view; the bit preserves the actual
   readable extent. On this LP64 ABI the tag occupies 32 bytes, at most 256 bytes
   for eight packed arguments. It remains a closed diagnostic representation.
3. The permitted nonblocking endpoint is a connected Unix datagram socket,
   rather than a pipe. Per-call `MSG_DONTWAIT | MSG_NOSIGNAL` avoids changing
   shared descriptor flags or SIGPIPE handling on assertion paths. A short send
   is treated as failed delivery; splitting a datagram would corrupt framing.
4. Without configuration, CHECK still evaluates and returns false but may have
   no external presentation. Fatal still publishes its packet and terminates.
   This intentionally supersedes the M1 blocking-stderr bring-up behavior.
5. The ordinary Logging fallback still has bounded write attempts but potentially
   blocking writes and unbounded EINTR draining of its own pending SIGPIPE.
   Assertions never invoke that fallback. This is not a new bounded Logging API.

## Deferred / unsupported

No crash uploader, collector process, registered callback chain, stack unwinder,
UI, ignore registry, async log flushing, runtime policy override, Windows/macOS
backend, or arbitrary formatter. The crash packet's visibility in an actual core
file depends on host dump policy. One Base runtime per process is required;
multiple copies across future independently linked plugins remain unsupported.

Only Linux LP64 was executed. LLP64, debugger detach races, arbitrary signal
handlers, process cancellation/longjmp during a failure, invalid text mappings,
and stack exhaustion are not covered by this ordinary assertion entry point.
`abort()` retains OS SIGABRT semantics; a hostile nonreturning installed signal
handler can prevent completion, and no OS scheduling deadline is promised.

## Build-cost measurements

Clang 18.1.3, CMake 3.29.6, Linux x86-64 LP64; libstdc++ shared runtime
`libstdc++.so.6.0.33` (GCC 14), selected GCC 13 headers. Five alternating runs,
cache disabled, `-O0 -g0 -ftime-trace`; standard deviation is run variation,
not a confidence interval. Full samples: `out/assert-audit/measurements/measurements.json`.
These are synthetic measurements, not engine frame-time predictions.

| Case | Frontend median ± SD (ms) | .text bytes | .rodata bytes | Function instantiations |
|---|---:|---:|---:|---:|
| empty | 0.5 ± 0.1 | 0 | 0 | 0 |
| include-only | 8.3 ± 0.5 | 0 | 0 | 0 |
| format-include-only | 11.9 ± 1.4 | 0 | 0 | 0 |
| logging-include-only | 1478.1 ± 77.6 | 0 | 0 | 298 |
| 1000-plain-macro | 176.7 ± 13.7 | 97580 | 12937 | 0 |
| 1000-plain-handwritten | 156.4 ± 13.8 | 87585 | 12937 | 0 |
| 1000-literal-macro | 175.3 ± 9.0 | 100580 | 12951 | 0 |
| 1000-literal-handwritten | 147.7 ± 37.7 | 90585 | 12951 | 0 |
| 1000-repeated-macro | 225.5 ± 15.2 | 116667 | 12946 | 1 |
| 1000-repeated-handwritten | 302.0 ± 15.9 | 149618 | 12946 | 0 |
| 1000-diverse-macro | 216.3 ± 12.0 | 120010 | 12946 | 4 |
| 1000-diverse-handwritten | 299.9 ± 22.9 | 152996 | 12946 | 0 |
| 10000-repeated-macro | 2882.6 ± 241.6 | 1169667 | 138946 | 1 |
| 10000-diverse-macro | 7494.2 ± 3189.9 | 1200010 | 138946 | 4 |

The plain-header median increment was 7.716 ms (target ≤10 ms). At 1000 sites,
plain macro/frontend ratio was 1.129 (target ≤1.15); repeated/diverse formatted
ratios were 0.747/0.721 (target ≤1.25). No parser/converter template is instantiated
at consumer sites. At 10,000 diverse sites host variability was very high; those
timings cannot support a precise scaling claim. Object text grows approximately
linearly with sites, while adapter instantiation count remains one/four.

The literal-message ratio was 1.187. Its handwritten baseline varied by 37.7 ms
SD against a 147.7 ms median; this does **not** certify a 15% budget for that
case. Repeat on a controlled host before treating that target as met; no budget
was raised and no PCH/template machinery was introduced to hide the result.

Unsanitized `-O2 -g0` linked probes: plain `.text` 9003 B / `.rodata` 592 B;
formatted `.text` 12411 B / `.rodata` 960 B. Dynamic-library contents are not
included in these section sizes. `nm -C` found one owner and one fatal packet
in each binary; plain had no parser or double-`to_chars` reference, formatted had
both. This verifies these static-link probes, not future DLL/plugin deployments.

Optimized plain and formatted CHECK probes branch directly to success with no
assertion runtime call/TLS/atomic/lock/poll. With an integer diagnostic argument,
Clang's formatted REQUIRE probe reserves a 40-byte frame and spills the input
even on success, due to its reference-taking adapter. It still performs none of
the prohibited runtime operations; do not advertise universally branch-only
code for `_F`. Plain REQUIRE remains compare/branch plus the caller's work.
Disabled plain/formatted ASSERT probes contain no unique diagnostic strings in
optimized objects or LTO-linked executables.

Incremental builds were executed, not inferred from Ninja dry runs. Touching only
`diagnostic_format.cpp` compiled exactly two objects (Base plus the isolated fake
backend copy), then relinked; no consumer compilation. Touching the format header
compiled six dependent objects; touching the plain header compiled ten. Logs are
`out/assert-audit/incremental-*.log`. No production binary links the fake copy.

## Final requirements audit

| Requirement | Final status | Evidence strength |
| --- | --- | --- |
| Plain/header dependencies and closed formatter boundary | Satisfied and verified | Compile include-closure fixtures; 96/189-line public headers; standalone link-symbol probes. |
| No passing-path runtime coordination/calls | Satisfied in representative optimized probes | Source audit plus six `-O2 -g0` assembly probes. Compiler spills noted above; not a claim about every compiler optimization. |
| Evaluation/elision/macros/type rejection | Satisfied and verified | Native/Catch tests, compile-fail fixtures, explicit-bool, comma/else tests, disabled unknown identifiers and LTO residue checks. |
| Begin before optional values; fatal never resumes | Satisfied and verified | Recursive formatted/plain subprocess tests, minimal packet inspection, fake continuation and real GDB/SIGABRT. |
| CHECK result/budget/concurrent suppression | Satisfied and verified in exercised schedules | Barrier test, held reporting owner, recursive calls, 64-report budget and fatal bypass; TSan clean. |
| Required identity survives truncation | Satisfied and verified | Control-filled long file/expression fixture retains line/function/status/message; canary bounds tests. |
| Transport errors cannot block fatal completion on output | Satisfied for selected backend | Full blocking stderr, full/closed socket watchdogs, injected short/EINTR/EAGAIN/EPIPE sends; packet delivery status checked. OS scheduling/signal-handler caveats remain. |
| Independent Logging behavior | Satisfied and verified | Real initialization/shutdown and console-sink lock probe; source contains no normal logger calls from Base. |
| Owned packet and readiness ordering | Implemented, partly directly verified | Minimal acquire-reader thread, pre-message state, complete/delivery readiness at fake terminal boundary, secondary preservation. General ordering correctness inferred from release/acquire and immutable-region source; no live collector/core-dump integration test. |
| Allocation discipline | Verified for intercepted paths/inputs | First CHECK/fatal probes plus 10,000 deterministic float encodings; `--wrap` and Linux/glibc LD_PRELOAD jobs. Not a universal allocator or signal-safety proof. |
| SDK/configuration | Satisfied and verified for four variants | Generated-policy/override/variant fixtures and installed formatted consumer reporting actual runtime flavor/policy. |
| Build-cost budgets | Mostly supported; literal-message gate not certified | Five-run measurements and variance above; no budget increase. |
| LLP64, other OS, stack capture, collector | Intentionally deferred | No validated backend or supporting infrastructure; no false support claims. |

## Validation commands and results

All paths below are relative to the repository root. Commands ran with pinned
host-tool shims. Real socket, debugger, ASan/LSan and TSan runs required execution
outside this sandbox's socket/ptrace restrictions; no sanitizer was disabled to
make its runtime tests pass.

```bash
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/build linux-clang-development
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/build linux-clang-debug
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/build linux-clang-profile
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/build linux-clang-asan-ubsan
```

The final matrix used these exact commands for each of
`linux-clang-debug`, `linux-clang-development`, `linux-clang-profile`,
`linux-clang-release-assert-tests`, `linux-clang-asan-ubsan`, and
`linux-clang-assert-tsan` (existing configured build trees):

```bash
out/host-tools/venv/bin/cmake --build out/build/<configuration> -j2
out/host-tools/venv/bin/ctest --test-dir out/build/<configuration> --output-on-failure -j2
```

Final results: **12/12** for each ordinary flavor; **11/11** for ASan/UBSan and
**11/11** for TSan. The extra ordinary-flavor test is LD_PRELOAD shared-library
allocation interception, intentionally separate from sanitizer allocators.
TSan provides strong `new/delete` definitions; the allocation child leaves those
to TSan while retaining linked C allocator wrapping. Unsanitized/ASan runs test
its C++ replacements. Test exceptions remain confined to Catch executables.
Raw final logs: `out/assert-audit/<configuration>-{build,test}.log`.

```bash
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/install-sdk linux-clang-debug
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/install-sdk linux-clang-development
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/install-sdk linux-clang-profile
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/install-sdk linux-clang-release
```

All four installed consumers built and executed, including a formatted CHECK,
installed-header policy checks and emitted-runtime policy comparison. Consumer
sources control their own file-prefix mapping; the SDK does not export this
checkout's private build flags.

```bash
./scripts/check --format --fix
out/host-tools/venv/bin/cmake -S . -B out/build/linux-clang-development -DLUDUS_ENABLE_TIME_TRACE=OFF
./scripts/check linux-clang-development --tidy
python3 -m py_compile tests/assertions/*.py tests/build_contract/*.py scripts/python/engine.py
git diff --check
```

Formatting and full Clang-Tidy 18 passed. Time tracing was disabled in the ordinary
compile database because Clang-Tidy diagnoses unused `-ftime-trace` under Werror;
benchmark commands explicitly enable it themselves. Subsequent changed test TUs
were also rechecked individually with `clang-tidy -p out/build/linux-clang-development
--quiet --warnings-as-errors=* <file>`. Fuzzer source was checked with explicit
C++23/no-exception/Base include flags because it is not a normal CMake target.

Fuzzer build and run (seed corpus includes terminated valid braces, malformed
braces, embedded NUL, and oversized format memory):

```bash
out/host-tools/bin/clang++ -std=c++23 -fno-exceptions -O1 -g \
  -fsanitize=fuzzer,address,undefined \
  -Imodules/foundation/base/include -Imodules/foundation/base/src \
  -Iout/build/linux-clang-development/modules/foundation/base/generated/include \
  modules/foundation/base/tests/assert_format_fuzz.cpp \
  modules/foundation/base/src/diagnostic_format.cpp modules/foundation/base/src/assert.cpp \
  modules/foundation/base/src/diagnostic_output.cpp modules/foundation/base/src/diagnostics_linux.cpp \
  -o out/assert-audit/assert-format-fuzz
out/assert-audit/assert-format-fuzz out/assert-audit/fuzz-corpus -runs=100000 -max_len=4096
python3 tests/assertions/measure_plain.py out/build/linux-clang-development out/assert-audit/measurements
```

100,000 fuzz executions completed in five reported seconds without sanitizer
errors. Fuzz inputs always provide valid bounded storage; malformed pointers
are not made safe by this parser. The standalone runtime used by this fuzzer is
built with both ASan and UBSan, not an unsanitized linked archive.

Real debugger commands used `gdb -q -batch -x <commands> <native-child>`.
Command files disabled pagination/debuginfod, ignored startup inspection SIGTRAP
until `start require`/`start lifetime`, then enabled SIGTRAP stop/nopass and
continued. Fatal stopped for inspection, then SIGABRT, then terminated on another
continue. Debug CHECK stopped and exited normally; Profile CHECK exited normally
without an inspection stop. `/tmp/assert-gdb-*.log` records these runs.

## Final diff risks and review boundaries

The main operational review item is startup configuration: absent a configured
socket, diagnostics are packet-only for fatal and presentation may be absent for
CHECK. Configure once before worker launch, never concurrently with reporting,
and do not close/reuse the runtime-owned duplicate. The caller may close its own
original descriptor; installed-consumer tests exercise this lifetime.

The broad build/configuration changes visible in `git diff` originate in M0/M1;
this pass preserves them and adds the hardening/formatter/narrow Logging changes
listed above. `.gitignore` was already user-modified and remains untouched.
No unrelated pointer ownership fixes, logger redesign, error-to-assert migration,
new dependencies, or production test handlers were introduced. The two copies
of runtime source in test builds belong to separate native/fake executables.

Allocation interposition cannot observe arbitrary hidden allocator entry points
or prove all future standard-library versions allocation-free. Re-run it when
toolchains change. Stderr fallback is still potentially blocking for ordinary
Logging, and a configured datagram channel is lossy under pressure. An owned
packet is not durable storage. These limits and the noisy literal-message
build-cost result prevent treating this test matrix as production certification.
