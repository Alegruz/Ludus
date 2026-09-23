# Assertion subsystem: M0 + M1 implementation evidence

Implemented September 23, 2026 against `068618cc817a`. This records the delivered
slice of [the design](assertions.md) and [reviewed plan](assertions-implementation-plan.md).
It does not qualify the future transport, formatter, crash-reporting or plugin
architectures. The original reconciliation document remains a historical baseline.

## Delivered behavior

Include `<ludus/foundation/base/assert.hpp>` and link `Ludus::FoundationBase`:

```cpp
LUDUS_ASSERT(texture != nullptr);
LUDUS_REQUIRE(index < count, "Internal index invariant violated");
if (!LUDUS_CHECK(cache_is_consistent, "Discarding inconsistent cache"))
{
    DiscardCache(); // A valid recovery path is the caller's responsibility.
}
LUDUS_FATAL("Required engine state is irreparably inconsistent");
```

Plain messages are borrowed, readable, NUL-terminated `const char*`, including
literals; null means no message. Braces have no formatting meaning. Readable
temporary text stays alive through the synchronous Finish call. Neither deferred
ownership nor arbitrary pointer validation is provided. Calls belong in function
bodies (a function-body lambda can be used by an initializer). Parenthesize
conditions containing preprocessor-visible commas.

Enabled conditions use one explicit bool conversion. Passing calls evaluate no
message and call no runtime function. Disabled Assert expands to `((void)0)`;
neither condition nor message is evaluated, expanded into the result, or
semantically compiled. Required work must never be placed in Assert arguments.
Recoverable Check always evaluates its condition, including after report
suppression, and returns false on failure. Messages can be suppressed and must
not contain required side effects.

| Engine flavor | CMake configuration | Assert | Check inspection when attached |
| --- | --- | --- | --- |
| Debug | Debug | Enabled, fatal on failure | Break, then return false |
| Development | RelWithDebInfo | Enabled, fatal on failure | Break, then return false |
| Profile | RelWithDebInfo | Compiled out | No break; return false |
| Release | Release / MinSizeRel | Compiled out | No break; return false |

Require and Fatal remain fatal in every flavor. Detached/unknown debugger state
does not cause an inspection trap. Fatal always reaches `abort()` after a
debugger continues. Recursive or secondary fatal uses `_Exit(134)`; no cleanup
or C++ exception is part of termination. Missing files, malformed input, device
loss and other expected runtime failures still use ordinary error handling.

## Files and dependency boundaries

- `cmake/EngineTargets.cmake` and `EngineOptions.cmake` fix the verified test
  exception option-order defect: test targets opt in through a target property;
  engine and native death children remain compiled with exceptions off.
- `cmake/EngineBuildFlavor.cmake`, presets and generated `assert_config.hpp`
  distinguish flavor from optimization configuration. Profile no longer identifies
  as Development. No assertion policy is inferred from `NDEBUG`.
- `cmake/CheckSdkVariant.cmake.in`, manifest/package templates and SDK verification
  bind generated policy to a single installed variant. Incompatible or unversioned
  prefixes fail before installing targets. Multi-config generators and unsupported
  backends fail explicitly. Identical variants can be reinstalled.
- Base exports `assert.hpp`, `assert_config.hpp`, `compiler.hpp` and
  `diagnostic_output.hpp`. The hot header is template-free and includes only
  generated policy, compiler attributes and the existing `types.h` boundary.
  `DiagnosticText` is only a pointer/length aggregate reserved by the design;
  this slice adds no string conversion or formatting API.
- `src/assert.cpp` owns failure handling. Private `diagnostic_record.hpp` provides
  the owned fatal packet; `diagnostic_platform.hpp` and `diagnostics_linux.cpp`
  provide native ID, debugger query, breakpoint and termination primitives.
  `diagnostic_output.cpp` writes emergency bytes independently of Logging.
- Base links no higher engine module. Logging still publicly links Base; Platform
  stays above both. No normal logging, log flush, callback framework or sink is
  used by assertions. The existing Logging emergency writer is unchanged.
- ADR 0003, AGENTS and steering permit this narrow Base diagnostic path. Existing
  standard-library facilities (`atomic` and termination primitives) are private
  runtime dependencies; no new general container/string abstraction is introduced.

`__FILE__`, `__LINE__`, `__func__`, the original stringified condition and failure
kind are captured at the caller. Private `-fmacro-prefix-map` maps engine path
literals to repository-relative paths while preserving working DWARF source
lookup. Installed consumers supply their own source mapping; no developer source
root leaks through exported compile options. `std::source_location` is absent
from the assertion include graph.

## Failure protocol and limits

Begin runs before optional message evaluation. Constant-initialized trivial TLS
marks recursion; one nonwaiting `atomic_flag` owns report generation. Recursive
or competing Check suppresses presentation and its message without clearing
another entry's state. Fatal never waits for ownership. A completed Check releases
its own entry. The first 64 owned Check reports in a process are detailed, with
a suppression notice on the final one. Fatal bypasses that limit.

Fatal publishes owned minimal bytes before evaluating the message, then separately
publishes the completed report. Each region has its own release/acquire-ready
flag and is never reused after publication. The buffers are 512 and 2,048 bytes,
including terminators and truncation space. Check uses a synchronous stack report.
C-string reads stop at 1,024 bytes per field or report capacity. Control bytes
are escaped; truncation is marked. Invalid non-null pointers remain caller errors.
No collector or stable cross-process packet ABI is exposed.

Reports include mapped site, expression, function, native thread ID, revision,
flavor and policy. A Git revision is **not** an executable identity; the report
explicitly marks artifact identity and stack capture unavailable. Debugger stacks
were verified separately. Runtime sources disable sibling-call optimization to
retain the failure frames; arbitrary callers compiled with other options remain
outside that guarantee.

The Linux reference backend reads `TracerPid` only after failure. `/proc` failure
means unknown/detached behavior, not an unconditional trap. This heuristic has an
attach/detach race and cannot distinguish every ptracer from an interactive
debugger. The validated host is Linux x86-64 / Clang 18; Windows, macOS and other
architectures have no production-support claim.

**stderr delivery has no time or durability guarantee.** At most four write
attempts handle partial output, EINTR and other errors; a single write may block.
SIGPIPE is blocked only on the calling thread, with its previous mask, pending
SIGPIPE and errno preserved. A newly generated SIGPIPE from EPIPE is drained
before restoring the mask, so a closed pipe does not kill Check. That drain
retries EINTR until completed or another result: unlike the write-attempt cap,
this safety cleanup can be delayed indefinitely by a signal storm. This is an
explicit M1 transport limitation, not the design's future bounded-output promise.
No global signal handler or shared stderr flags are changed. Diagnostic output
is not promised async-signal-safe. A broken transport cannot invalidate the
already-published primary packet, but can prevent the process reaching termination.

No throw, longjmp, cancellation, coroutine suspension or thread exit may cross
Begin/Finish. Message expressions must be safe observations of the failing state.
This design does not make corrupted pointers, stacks or arbitrary engine state safe.

## Tests and review-sensitive changes

The new tests live in Base, `tests/assertions/`, `tests/build_contract/`, the SDK
consumer, and Logging's isolated lifecycle child. Coverage includes:

- Exactly-once conditions/messages, success elision, undeclared disabled operands,
  explicit bool, dangling else, comma and nested-macro expressions, constexpr
  pass/compile-fail, empty Fatal rejection and true/false Check results.
- Fresh native subprocesses for all fatal APIs, no return/cleanup, recursive and
  competing failures, ownership release, report budget, before-main/after-main,
  pre-logger-init/post-shutdown, truncation, escaping, metadata and closed stderr.
- Minimal packet readiness before message evaluation. An isolated fake backend
  simulates breakpoint continuation but still calls the real abort primitive.
  A linker wrapper injects eight EINTR results into SIGPIPE draining; production
  code has no test behavior switch.
- Actual compiler exception-feature probes, flavor/config rejection, generated
  SDK policy agreement, forbidden overrides and NDEBUG independence across TUs.
- Include closure, optimized assembly and absence of unique disabled condition
  and message strings. Sanitizers are disabled **only** for these standalone
  assembly probes; runtime and death tests remain instrumented.
- Isolated C++ new and linked malloc/calloc/realloc interception on passing and
  first-failure paths. This does not intercept arbitrary internal libc allocations
  or qualify a future dynamic-loader/plugin TLS model.

Profile and Release revealed existing Logging tests that supplied compile-stripped
Info/Debug macros to tests of sink/runtime behavior. Those fixtures now call the
existing `Log` function at their original levels. Dedicated macro-filtering tests
remain intact. No production logging filter, shutdown or sink behavior changed.

CI adds explicit Debug/Profile/Release test jobs alongside existing Development
and ASan/UBSan coverage. Tests are enabled in a separate Release tree; the normal
Release preset still omits tests. Native children never enable C++ exceptions.

## Validation record

The command/result and measurement record below is the completion gate for this
slice. Generated logs, code-generation probes and measurement artifacts stay in
`out/` or `/tmp`, not in the public SDK.

Toolchain: Ubuntu Clang/LLD 18.1.3, CMake 3.29.6, Ninja 1.11.1.3, Python 3.12.3,
GDB 15.1. `./scripts/doctor` passed required tools; optional ccache, LLDB and rr
were absent. `./init.sh --no-system-install` refreshed preset bootstrap state.

All tested engine trees ultimately had `LUDUS_WARNINGS_AS_ERRORS=ON`. Existing
Debug/ASan/Release caches retained OFF despite `CI=true`, so these were explicitly
reconfigured before the final warning-clean builds. Build parallelism was 2.

```bash
out/host-tools/venv/bin/cmake --preset linux-clang-debug -DLUDUS_WARNINGS_AS_ERRORS=ON
out/host-tools/venv/bin/cmake --preset linux-clang-asan-ubsan -DLUDUS_WARNINGS_AS_ERRORS=ON
out/host-tools/venv/bin/cmake --preset linux-clang-release -DLUDUS_WARNINGS_AS_ERRORS=ON
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/test linux-clang-debug
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/test linux-clang-development
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/test linux-clang-profile
out/host-tools/venv/bin/cmake --preset linux-clang-release -B out/build/linux-clang-release-assert-tests -DLUDUS_BUILD_TESTS=ON -DLUDUS_WARNINGS_AS_ERRORS=ON
CMAKE_BUILD_PARALLEL_LEVEL=2 out/host-tools/venv/bin/cmake --build out/build/linux-clang-release-assert-tests
CI=true out/host-tools/venv/bin/ctest --test-dir out/build/linux-clang-release-assert-tests --output-on-failure
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/build linux-clang-asan-ubsan
CI=true out/host-tools/venv/bin/ctest --test-dir out/build/linux-clang-asan-ubsan --output-on-failure
CI=true ./scripts/check linux-clang-development --all
```

Results: **10/10 CTest entries passed in each of Debug, Development, Profile,
Release-with-tests and ASan/UBSan**. The first sandboxed ASan run could not run
LeakSanitizer under ptrace. The final sanitizer suite ran outside that sandbox
with leak detection enabled, not suppressed. Full format/tidy passed; the final
Logging lifecycle fixture strengthening also passed focused clang-tidy and format.

The fresh build-budget profile leaves time tracing enabled in Development.
Compiler-only fixtures initially rejected that output-only flag with
`-fsyntax-only`; the fixture extractor now removes it without removing policy or
warning flags. The complete profiled Development suite was rerun with:

```bash
CI=true out/host-tools/venv/bin/ctest --test-dir out/build/linux-clang-development --output-on-failure
```

Result: **10/10 passed**. No runtime behavior or diagnostic was suppressed to
accommodate these test/configuration issues.

```bash
out/host-tools/venv/bin/cmake --preset linux-clang-development -B out/build/linux-clang-assert-tsan -DLUDUS_ENABLE_TSAN=ON -DLUDUS_ENABLE_ASAN=OFF -DLUDUS_ENABLE_UBSAN=OFF -DLUDUS_BUILD_SMOKE_APP=OFF -DLUDUS_WARNINGS_AS_ERRORS=ON
CMAKE_BUILD_PARALLEL_LEVEL=2 out/host-tools/venv/bin/cmake --build out/build/linux-clang-assert-tsan --target ludus_assert_child ludus_assert_fake_child
CI=true out/host-tools/venv/bin/ctest --test-dir out/build/linux-clang-assert-tsan -L assert-concurrency --output-on-failure
```

Result: **2/2 native/fake subprocess suites passed under TSan**, outside the
sandbox. The harness rejects sanitizer diagnostics even when termination status
matches an expected fatal signal. This is focused concurrency coverage, not M2
stress or publication qualification.

```bash
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/install-sdk linux-clang-debug
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/install-sdk linux-clang-development
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/install-sdk linux-clang-profile
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/install-sdk linux-clang-release
```

All four installed Base-only consumers built and ran, reporting the matching
runtime flavor/assert/break policy; their two TUs use different NDEBUG states.
Public headers and generated policy installed; internal headers did not. Export
inspection confirmed no Base dependency on Logging/Platform and no source/build
paths in exported include/compile options. The pre-existing unversioned Development
install was preserved as `out/install/linux-clang-development-before-assertions`.

A real temporary Development installation followed by a Profile install to the
same prefix failed at the guard; SHA-256 digests of all 27 installed files were
unchanged. Script-level negative fixtures also cover incompatible and unversioned
manifests. These validate ordinary packaging errors; not arbitrary filesystem
tampering or concurrent installers.

Native GDB tests ran against both Debug and optimized Development children:
`start require`, stop on SIGTRAP, inspect the caller and published complete packet,
`continue`, observe SIGABRT, then `continue` to actual process termination. The
caller line and Base runtime sources resolved without a source-path substitution.
Separate `start lifetime` tests showed Debug's Check breakpoint returning to
normal process exit, and Profile's Check returning without an inspection trap.
Logs: `/tmp/ludus-assert-gdb-{debug,development,check-debug,check-profile}.log`.

The automated `-O2 -g0` probes contain no passing-path TLS, atomic, allocator,
clock, debugger query or runtime call. The enabled Assert probe tests/branches
before setting up its failure stack frame; the disabled probe is only the
caller's `value + 1` and return. The representative enabled object has 81 bytes
of `.text` and 52 bytes of string data, including the caller's return code; the
disabled object has 4 bytes of `.text` and no string section. These are one
x86-64 compiler example, not a per-site universal binary budget. No LTO result
is claimed. Probe artifacts: `out/build/<tree>/tests/assertion-codegen/`.

Actual touch/rebuild experiments (restoring source mtimes afterward) confirmed:
`assert.cpp` rebuilt only its Base object and isolated fake-backend copy; public
`assert.hpp` and generated policy rebuilt the seven actual including TUs, leaving
unrelated Logging/Platform implementation TUs alone. Ninja dry runs overestimated
the set because of CMake's dynamic dependency restat, so the recorded evidence
uses real builds: `out/profile/assertion-rebuilds/*-actual.txt`.

```bash
CMAKE_BUILD_PARALLEL_LEVEL=2 CI=true ./scripts/check-build-budget --profile
python3 tests/assertions/measure_plain.py out/build/linux-clang-development out/profile/assertions-plain-paired
```

The engine-wide budget passed unchanged: **49.6 s summed frontend parsing versus
the 65 s limit**, with all listed project headers within their existing budgets.
This is a fresh absolute measurement, not a controlled before/after engine-build
comparison. The assertion header is 96 lines including comments/blank lines;
include-closure tests find none of the prohibited heavy, OS or Logging headers.

The plain benchmark uses Clang 18, `-O0 -g0 -ftime-trace`, no compiler cache, five
alternating runs of each macro/handwritten pair and 0/100/1,000/10,000 sites.
Raw runs, standard deviations, frontend/backend times, peak RSS, object/section
sizes and template counts are in `out/profile/assertions-plain-paired/measurements.json`.
The same include is used in both nonempty variants. Representative medians:

| Sites/message | Macro frontend (ms) | Handwritten frontend (ms) | Macro/backend (ms) | Handwritten/backend (ms) | Macro/handwritten RSS (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 100, none | 32.5 | 29.6 | 12.5 | 8.8 | 89.2 / 89.0 |
| 100, literal | 38.7 | 28.1 | 10.6 | 7.7 | 89.2 / 88.9 |
| 1,000, none | 182.0 | 269.5 | 83.2 | 76.8 | 102.1 / 100.0 |
| 1,000, literal | 185.6 | 171.4 | 82.2 | 56.6 | 102.3 / 99.7 |
| 10,000, none | 3,373.1 | 2,753.7 | 1,356.1 | 916.8 | 237.4 / 215.5 |
| 10,000, literal | 2,151.6 | 2,031.0 | 885.1 | 677.9 | 237.9 / 214.7 |

There are **zero class/function template instantiations** in every fixture.
At 1,000 plain sites, unoptimized macro `.text` is 97,580 bytes versus 87,585
handwritten; `.rodata` is identical at 12,943 bytes. The macro's statement wrapper
adds unoptimized control-flow code; these figures must not be substituted for
the optimized success-path results above. No linked-engine size improvement is
claimed.

**The fine-grained compile-time gate remains inconclusive on this host.** Empty
frontend time ranged from 0.496 to 24.076 ms; include-only from 10.061 to 48.488
ms. Their median difference is **10.989 ms**, slightly above the design's 10 ms
limit. Earlier unpaired runs gave 7.067 and 14.168 ms; none was selected to make
the gate pass. The final 1,000-site ratios nominally meet the 15% target (0.675
plain, 1.083 literal), but the handwritten plain frontend ranged from 151.6 to
578.2 ms, so the apparent speedup is not credible evidence of an improvement.
Rerun the checked-in paired harness on a controlled runner before declaring the
10 ms/15% microbudgets qualified. Budgets were not raised. The broader engine
budget, include graph, zero instantiation count and rebuild-isolation gates did
pass; they do not erase this measurement limitation.

Final review also ran `git diff --check`, parsed all modified/new Python modules,
and checked local links in the implementation/build documentation. The pre-existing
`.gitignore` edit was left intact. No complete M2+ implementation or unrelated
Foundation utility refactor is present.

## Deferred work and next step

M2 qualifies transport, additional injected faults, long-running concurrency and
publication under stress. The current smoke/concurrency/death checks do not prove
bounded fatal completion or delivery to an external process. M3 adds opt-in tagged
formatting; M4 may adapt Logging's byte writer and measure broader integration.
There is no formatter framework, crash uploader, dialog, ignore registry, generic
handler chain, stack symbolication, remote collector or async logging bridge.
No engine-wide assertion migration was performed.

The static SDK assumes one Base runtime copy per process. Linking independent
copies into plugins can duplicate ownership and budgets; resolve that packaging
model before promising plugin support. Deliberately undefining generated policy
or manually mixing archive/header variants is unsupported; installation checks
and compile-time override rejection prevent ordinary configuration mistakes, not
hostile preprocessing. Sanitized SDK export/consumer flags and a full MinSizeRel
SDK are not qualified here (MinSizeRel policy/configuration is tested).

The next implementation milestone is **M2**, not another public API redesign.
Choose a transport whose lifetime, backpressure and shutdown behavior can actually
be guaranteed before advertising bounded failure completion or durable reports.
