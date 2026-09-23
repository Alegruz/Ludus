# Assertion subsystem: repository reconciliation and implementation plan

Status: historical reconciliation and milestone contract. M0a, M0b and M1 have
now been implemented; see [implementation evidence and remaining limits](assertions-m0-m1.md)
before interpreting the original findings or executing the next-step recommendation.
M2/M3 and narrow M4 integration have subsequently been implemented; see the
[adversarial review](assertions-adversarial-review.md) for current evidence and
limitations. M5 remains deferred. The original milestone text below is retained
as the reviewed plan, not an instruction to repeat completed work. The behavior contract remains
[assertions.md](assertions.md); this document supplies checkout evidence,
sequencing, and review gates rather than a replacement design.

Inspected September 23, 2026 at `269b6118a6ae`. The design inspected
`64e5bfd36a2e`. Existing local documentation and `.gitignore` changes belong to
other work and are outside this plan. During inspection, HEAD advanced to
`068618cc817a` with a separate logging-documentation commit; its diff contains
no engine/build changes, so the source/configuration findings remain current.

**Recommendation:** start with M0a, a narrowly tested correction to the test
exception configuration, then M0b, explicit build flavor and generated SDK
assertion policy. Deliver the plain Linux assertion path in M1 only after both
pass. Do not first refactor Logging, Platform, strings, or the legacy defines
headers. No repository fact currently forces a change to the design's core
architecture.

## 1. Repository findings

### Evidence and inspection scope

The complete assertion design and its [Rabin review](assertions-rabin-review.md)
were read, together with:

- [AGENTS.md](../../AGENTS.md), [steering rules](../../.kiro/steering/coding-standards.md),
  [standard-library policy](../decisions/0003-standard-library-usage-policy.md),
  [logging type erasure](../decisions/0004-logging-format-type-erasure.md),
  [build budgets](../decisions/0005-build-time-budgets.md),
  [build profiling](../development/build-profiling.md), and
  [build instructions](../development/building.md).
- All current Base public/private sources and tests; Logging's public API,
  runtime, formatter, emergency output, sinks, and tests; Platform's target,
  configuration, and window implementations.
- Root/module/test CMake files, engine options/defaults/warnings/sanitizers,
  generated metadata/package/manifest templates, presets, Conan recipe/profile,
  tool configuration, SDK consumer, orchestration script, and CI workflow.

Evidence below comes from source inspection plus a **fresh, isolated Profile
configure**, generated compilation database/export files/target graph, and
compiler probes. This was not a full engine build or a runtime qualification.
Historical profiling figures are not new measurements.

### Actual target and include graph

| Target | Actual dependencies and ownership | Assertion consequence |
| --- | --- | --- |
| `Ludus::FoundationBase` | Static; `src/version.cpp`; private build-only options/warnings interfaces; no engine module dependency | Add runtime sources to this existing target. No new Diagnostics library. |
| `Ludus::FoundationLogging` | Static; publicly links Base; sinks and record interfaces private | Can consume a small Base emergency-output API in a later integration change. |
| `Ludus::Platform` | Static; publicly links Base and Logging; optional private Wayland dependency, otherwise headless | Windowing is above Base. Do not reuse its public configuration header from assertions. |
| Base/Logging tests | Catch2 executables with project defaults, linked to the respective engine libraries | Production and test compilation policies must remain distinct. |

[Base CMake](../../modules/foundation/base/CMakeLists.txt) exports a public
`FILE_SET`: `defines.h`, `types.h`, `version.hpp`, generated
`build_metadata.hpp`, and `pointer.hpp`. It does **not** export `defines.hpp`.
Private `src` include directories and project options are not part of the
installed API. The freshly generated export confirms there is no exported
assertion policy and no Base dependency on Logging/Platform.

`types.h` is the required, acceptable `<cstdint>`/`<cstddef>` boundary.
`defines.h` also includes `<type_traits>` for `DerivedFrom`; `defines.hpp`
duplicates inline/nullptr helpers and infers legacy build flavor from debug
macros. Neither belongs in the new assertion include chain. Add only the cold
and noinline helpers actually needed to `compiler.hpp`. There is no prerequisite
to consolidate all existing helpers or modify pointer ownership behavior.

There is no engine allocator, native general-purpose string/view, crash
reporter, stack-capture service, or assertion implementation to reuse. Searches
found no existing `LUDUS_ASSERT`, `LUDUS_REQUIRE`, `LUDUS_CHECK`, `LUDUS_FATAL`,
`LUDUS_COLD`, or `LUDUS_NOINLINE` definitions. `LUDUS_LOG_FATAL` is a distinct,
returning logging operation; Catch2's unprefixed `CHECK`/`REQUIRE` do not collide.
Window failures already use explicit return values/out-parameters and should
remain ordinary control flow.

### Logging cannot be the failure runtime

[log.hpp](../../modules/foundation/logging/include/ludus/foundation/logging/log.hpp)
still includes formatting/source-location/string-view/utility machinery, and
its configuration transitively includes `<filesystem>`. Type erasure moved
heavy formatting work into `VLog`; it did not make that header suitable for
Base. `VLog` uses `std::vformat` and an owning string. Runtime dispatch/flush use
a shared mutex and invoke synchronous sinks. File output can format, allocate,
rotate files, and block. The asynchronous mode is a stored setting/reserved
API, not an implemented queue or bounded drain protocol.

[EmergencyLog](../../modules/foundation/logging/src/emergency_logger.cpp) builds
a 512-byte record and uses `snprintf`, `fwrite`, and `fflush`, with Windows
debugger output in its platform branch. Fatal logging reaches this emergency
copy after ordinary dispatch/flush, then returns. None of that provides a safe
assertion implementation. Logging's thread identity also initializes a TLS
object containing an owning string and uses an engine counter, not a native
thread ID. The assertion runtime must obtain its own minimal native identity.

Use an independent Base writer first. Later share only the final byte-output
boundary; keep Logging's record construction private. Do not call normal log
macros or wrap synchronous `Flush` in a timeout and describe it as bounded.

### Build flavor and SDK facts

| Preset | CMake configuration | Current flavor define | Required assertion policy: enabled / Check break |
| --- | --- | --- | --- |
| `linux-clang-debug` | `Debug` | `LUDUS_BUILD_DEBUG` | `1 / 1` |
| `linux-clang-development` | `RelWithDebInfo` | `LUDUS_BUILD_DEVELOPMENT` | `1 / 1` |
| `linux-clang-asan-ubsan` | `RelWithDebInfo` | `LUDUS_BUILD_DEVELOPMENT`, sanitized | `1 / 1` |
| `linux-clang-profile` | `RelWithDebInfo` | **`LUDUS_BUILD_DEVELOPMENT`** | `0 / 0` |
| `linux-clang-release` | `Release` | `LUDUS_BUILD_RELEASE` | `0 / 0` |
| No committed MinSizeRel preset | `MinSizeRel` | `LUDUS_BUILD_RELEASE` | `0 / 0` |

`CONFIG:Profile` in [EngineOptions.cmake](../../cmake/EngineOptions.cmake)
does not match the actual Profile preset. Fresh Profile compilation commands
contain `-O2 -g -DNDEBUG`, `-DLUDUS_BUILD_DEVELOPMENT=1`, and `-UNDEBUG`.
The latter currently applies to every configuration except exactly `Release`,
including MinSizeRel. Fix flavor identification; assertion policy must never
depend on these `NDEBUG` details. Removing `-UNDEBUG` globally is unnecessary
and would alter unrelated existing behavior.

All committed presets use single-configuration Ninja. Release disables tests.
ASan/UBSan has a preset; TSan is an option with no committed preset. ASan and
TSan cannot be combined. Root CMake requires 3.29 and C++23 with extensions off.
Managed versions are CMake 3.29.6, Ninja 1.11.1.3, Conan 2.8.1; the reference
compiler family is Clang/LLVM 18. The tool-version file actually specifies
minimum LLVM versions, not an exact patch-version lock. Conan selects Linux
x86-64, Clang 18, libstdc++ and Catch2 3.4.0.

[SDK validation](../../scripts/python/engine.py) currently checks selected
artifacts, rejects installed `internal` headers and some source-tree paths,
and compiles a version-only consumer linked to Base. Its consumer is always
configured as **Release**, even for a Development SDK. This is useful coverage
to retain: client optimization configuration must not silently change the
SDK's assertion policy. It is not itself proof of an SDK mismatch. The existing
manifest records neither build flavor nor assertion policy. No check proves
generated header/archive consistency or prevents installing different variants
over the same include tree.

### Newly verified prerequisite: test exceptions are not enabled

[ludus_enable_test_exceptions](../../cmake/EngineTargets.cmake) claims to append
`-fexceptions` after inherited `-fno-exceptions`. The fresh compilation database
shows the opposite order for Base tests:

```text
... -std=c++23 -fexceptions -fno-exceptions -UNDEBUG ...
```

A temporary `throw` syntax probe using those test options failed with
“exceptions disabled”; appending a final `-fexceptions` made that same probe
compile. Engine options correctly reject the probe. This is a build-policy
defect, not evidence that all existing tests currently fail. Passing ordinary
tests would not detect it. Correct the helper/options interaction before
relying on new Catch2 failure tests. Keep native fatal child binaries and
production code exception-free.

### Build-time checks have advanced, but do not replace assertion measurements

Commit `269b6118a6ae` added ADR 0005, the budget configuration, a check command,
CI coverage, and stronger contributor guidance. The design's engine-source
observations otherwise still match: the intervening commit did not change the
Base/Logging/Platform implementations.

The current budget gate parses ClangBuildAnalyzer's **Expensive headers**
report, filters source headers under `modules/`, and compares aggregate frontend
time. It does not exhaustively audit includes, and generated headers outside
`modules/` are not covered by that filter. Its current default is 2,000 ms per
reported header and 65 seconds aggregate frontend time, with logging overrides.
Those regression budgets cannot establish the assertion design's 10 ms
empty-header target, absence of prohibited transitive includes, or template and
per-site costs. Keep the existing gate and add focused assertion measurements;
do not expand this task into generic include-lint infrastructure.

`profile-build` deletes the selected build tree. Its wrapper accepts only the
presets in `PRESET_BUILD_TYPES`, not arbitrary new names. Reserve an existing
profile tree for engine-wide comparisons, or invoke tools directly in an
isolated benchmark tree. Do not propose a new profiling preset without also
accounting for Conan/bootstrap orchestration.

## 2. Design reconciliation

Statuses distinguish an implementation still to be written from an actual
prerequisite or stale observation.

| Design requirement | Current repository evidence | Status | Required action |
| --- | --- | --- | --- |
| Runtime at/below Base; no upward dependency | Actual CMake graph has Base below Logging/Platform | already satisfied | Keep runtime in Base; verify graph/export again in M1. |
| Lightweight alias boundary | `types.h` contains required aliases | already satisfied | Reuse it; no string/allocator project first. |
| Tiny compiler helper/header graph | Two legacy defines headers; neither has the new cold helpers | implementation work | Add narrow `compiler.hpp`; avoid both legacy includes. |
| Template-free plain macros | No existing assertion API/naming conflict | implementation work | M1 implements the contract, not a handler framework. |
| Explicit SDK assertion policy | Private build-only project options; manifest has no policy | prerequisite required | M0b generates/installs numeric config and validates packaging. |
| Profile differs from Development | Both presets use RelWithDebInfo; fresh Profile command defines Development | prerequisite required | M0b makes flavor explicit without changing optimization configuration. |
| Engine exceptions off, tests on | Test options end in `-fno-exceptions` | design document stale | Correct the design's factual statement when M0a lands; add compiler-level regression coverage. |
| No argument evaluation/semantic compilation when disabled | No assertion implementation yet | implementation work | Compile positive and negative fixtures with actual variant headers. |
| Guard before diagnostic evaluation | No runtime yet | implementation work | Two-stage Begin/Finish and minimal recursion/ownership in M1, not postponed to M2. |
| Fatal cannot resume; Check returns false | Current fatal *logging* returns | implementation work | Separate APIs; native subprocess tests, debugger continue test. |
| Independent emergency diagnostics | Logging's private emergency path uses stdio | implementation work | New Base writer M1; narrow Logging adaptation M4. |
| Policy permits sanctioned Base failure path | ADR 0003 routes diagnostics through Logging | prerequisite required | M0b documents this specific exception; ordinary diagnostics still use Logging. |
| Synchronous logger cannot supply bounded flush | Locks, string allocation and synchronous sinks still present | intentionally deferred | No assertion logging hook until a proven suitable backend exists. |
| Constant TLS, nonwaiting owner, Check report budget | No reusable service exists | implementation work | M1 basic correctness; M2 publication/fault/concurrency qualification. |
| Opt-in closed typed formatter | No native general-purpose formatter/view; Logging is too heavy | implementation work | M3 only; private numeric conversion policy amendment then. |
| Public headers explicitly installed | Base public file set is explicit, omits `defines.hpp` | implementation work | Add only the new public files, test source and installed includes. |
| Installed consumer validates assertion variants | Consumer tests version only and is always Release | prerequisite required | M0b checks policy; M1 adds runtime/ODR fixtures. Retain cross-config consumer coverage. |
| Build time is a gate | New ADR 0005 and budget CI exist | design document stale | Supplement the design's measurement procedure with this gate and its coverage limits. |
| Native Linux first | Linux presets/Wayland/headless; no diagnostic OS abstraction | already satisfied | Private Linux backend; unsupported ports explicitly unavailable. |
| Death tests and release-policy tests | No death harness; Release disables ordinary tests | implementation work | M1 process harness and a separate Release tree with tests enabled. |
| Native debugger/core/QA handoff | No collector or symbol archival service in tree | intentionally deferred | Test Linux debugger behavior; qualify deployment capture separately in M4/M5. |
| One runtime across future shared modules | Current libraries are static; no plugin runtime arrangement | intentionally deferred | Single executable now; establish unique ownership before any plugin/DLL support. |

Do not rewrite the design merely because implementation has not started. Its
API, source-location choice, bounded formatter, termination policy, and layering
remain appropriate. When implementation lands, update its inspected revision,
test-exception statement, budget-gate context, and implementation status with
actual results. This plan deliberately leaves the primary contract untouched.

## 3. Prerequisites and scope boundaries

Only two prerequisite changes are needed. Review them independently.

1. **M0a: correct the existing test-only exception policy.** Keep all engine
   TUs on `-fno-exceptions`. Use a small target property consumed by the project
   options to exclude that flag for explicitly designated Catch2 targets;
   the test helper sets that property and supplies their exception-enabled
   option. Applying the policies directly in target defaults is an alternative,
   but would move more existing policy. The property approach retains current
   ownership and avoids relying on the relative order of direct and transitive
   compile options. Verify its evaluation using actual generated commands.
2. **M0b: establish one variant contract before macros depend on it.** Add
   `LUDUS_BUILD_FLAVOR` (`Debug`, `Development`, `Profile`, `Release`) explicitly
   to every committed preset. Derive exactly one engine flavor define from
   it. Keep CMake configurations and Conan build types unchanged. Validate
   compatible pairs: Debug/Debug; Development or Profile/RelWithDebInfo;
   Release/Release or MinSizeRel. Require explicit flavor for direct non-preset
   configuration too, with a clear diagnostic and documented command. A
   one-time default from conventional configurations is possible, but explicit
   input is simpler to audit and avoids ambiguous RelWithDebInfo intent. Never
   silently preserve a conflicting cached flavor.

M0b generates `assert_config.hpp` with numeric `LUDUS_ENABLE_ASSERTS` and
`LUDUS_BREAK_ON_CHECK` from the table above. They are outputs of the flavor
policy, not independent per-TU knobs. Reject predefinitions of these public
policy macros, invalid values/flavors, and contradictory configuration inputs.
There is no fallback to `NDEBUG` or `defines.hpp`. Add flavor/policy schema
version/values to the SDK manifest and package metadata. Compile the source
library and installed consumers against the same generated file.

Continue using separate build and install roots per flavor. Until separate
generated roots and packages for multi-config generators are implemented and
tested, reject those generators explicitly for this SDK configuration. This
limits new functionality to the repository's actual Ninja workflow; it is not
a claim of multi-config support. Add an install-time variant check before any
files are overwritten, including direct `cmake --install`; conflicting or
unversioned old manifests require a clean, separate prefix. Test installation
into a fresh prefix and refusal to mix Development/Profile despite their shared
RelWithDebInfo archive configuration.

Do not export all project flags to solve assertion configuration. Consumer
warnings, optimization and engine-only flags are separate concerns. Package
tests must also compare manifest/header values; merely locating the header is
insufficient. Supported builds reject ordinary overrides, but cannot prevent a
consumer from deliberately editing headers or undefining policy after inclusion.
Such mixing is unsupported and can violate ODR; no success-path runtime
fingerprint check is proposed.

Amend ADR 0003 narrowly for the Base emergency byte path in M0b. Reserve the
implementation-only `<charconv>`/`to_chars` decision for M3 with its allocation,
binary-size and toolchain evidence. These are policy alignments required by the
accepted design, not permission to spread printf diagnostics or STL machinery.

Not prerequisites: cleaning up both defines headers; replacing Logging's
strings/containers/formatting; removing all `std::unique_ptr` use; fixing every
old README; building an allocator, job system, crash collector, async logger,
or universal compiler/platform framework. Removing the null-deletion TODO is
a later small migration edit, not a reason to change `UniquePtr` semantics.

## 4. Reviewable implementation milestones

Paths here are repository-relative; **new file names are proposed**, not claims
that these APIs or tests already exist. All production additions follow the
aliases, naming, `#pragma once`, warning and no-exception rules. Each milestone
records results before the next expands the surface.

### M0a — correct test-only exception policy

**Likely files:** `cmake/EngineOptions.cmake`, `cmake/EngineTargets.cmake`,
`tests/CMakeLists.txt`, new `tests/build_contract/verify_compile_policy.py` and
small compile fixtures under `tests/build_contract/`.

**Behavior:** Catch2 test TUs really have exceptions enabled; engine and native
death-child TUs remain disabled. Use a regression fixture that checks compiler
feature macros and compiles an exception construct under a test target. An
engine-policy negative fixture must reject that construct. Negative tests must
fail for the expected reason, not a missing include or broken tool invocation.

**Gates:** inspect the actual compile commands for Base, Logging, smoke, and
both test targets; run their existing tests in Debug, Development, Profile and
ASan/UBSan; configure/build Release as well. A temporary deliberately failing
Catch2 test must be reported as a test failure, with the expected test cleanup,
not confused with an engine fatal-path test. Add permanent policy regression
checks without leaving a failing test in the normal suite. Format/tidy for
added C++ fixtures and warning-clean builds apply.

**Non-goals/deferred:** no assertion API, config header, formatter, logger
change, engine exception enablement, or death-test catch/throw workaround.
Acceptance is generated-code policy evidence, not a corrected comment alone.

### M0b — explicit flavor, assertion configuration and SDK identity

**Likely files:** `CMakePresets.json`, `CMakeLists.txt`,
`cmake/EngineOptions.cmake`, new `cmake/assert_config.hpp.in`,
`cmake/LudusSdkManifest.json.in`, `cmake/LudusConfig.cmake.in`,
`modules/foundation/base/CMakeLists.txt`, `scripts/python/engine.py`
(SDK verification only), `tests/CMakeLists.txt`, `tests/build_contract/`,
`tests/sdk_consumer/CMakeLists.txt`, `tests/sdk_consumer/main.cpp`,
`.github/workflows/ci.yml`, `docs/development/building.md`, and ADR 0003.
An install guard may be a small private CMake script under `cmake/`.

**Behavior:** implement the prerequisite contract above. Install the tiny
generated header in Base's existing public file set. Profile remains optimized
RelWithDebInfo but now advertises Profile. Keep log filtering independent.
Register configuration/compile fixtures with a `build-contract` CTest label.
Document the manual configuration argument and prefix rules.

**Tests/gates:** all five committed presets produce expected flavor and numeric
policy; MinSizeRel with Release flavor gives `0/0`; each invalid pair/value and
public predefinition is rejected. Two installed-consumer TUs agree under both
defined and undefined `NDEBUG`. Reconfigure a tree across incompatible values
and verify an explicit error rather than stale policy. Install Debug,
Development, Profile and Release to separate prefixes; verify manifest, header,
archive/package provenance and no private/source/build-directory include leak.
Retain a Release consumer of the Development SDK and assert policy `1/1`.
Verify conflicting-prefix install fails **before** overwriting artifacts.
Existing unit/ASan/UBSan and SDK consumer checks stay green.

**Non-goals/deferred:** still no runtime or macros. No global legacy-header
cleanup, new CMake configuration named Profile, or multi-config SDK promise.
No sanitizer export redesign: sanitizer consumer checks must explicitly use
matching sanitizer flags, since these are not currently exported.

### M1 — smallest correct plain Linux vertical slice

**Likely files:** Base `CMakeLists.txt`; new public
`include/ludus/foundation/base/{compiler.hpp,assert.hpp,diagnostic_output.hpp}`;
new `src/{assert.cpp,diagnostic_output.cpp,diagnostics_linux.cpp}` and
`src/internal/{diagnostic_record.hpp,diagnostic_platform.hpp}`; new Base tests
`tests/assert_tests.cpp`, `tests/assert_death_child.cpp`,
`tests/assert_platform_fake.cpp`; compile fixtures under `tests/assertions/`;
test registration in `tests/CMakeLists.txt`; SDK consumer and verification.

**Behavior:** implement plain `LUDUS_ASSERT`, `LUDUS_REQUIRE`, `LUDUS_CHECK`,
`LUDUS_FATAL`, literal-message variants, site aggregate, expression text and
`__FILE__`/`__LINE__`/`__func__` capture. Keep real behavior out of macros.
`assert.hpp` includes only `types.h`, `compiler.hpp`, generated config. Use the
design's two-stage Begin/Finish entry points: establish state and copy/publish
the minimal fatal site **before evaluating diagnostic arguments**.
Keep the design's under-200-non-comment-line plain header target. Configure
repository-relative source paths with private compiler prefix mapping for
engine builds; do not export a developer's absolute source root to consumers.
Installed consumers own their source mapping. Test both call-site locations,
including Check's expression form, so a helper lambda cannot substitute its
own function name for the enclosing caller's.

Correctness cannot wait for M2: M1 includes constant-initialized trivial TLS,
one nonwaiting `atomic_flag` report owner, recursive/concurrent Check
suppression, immediate secondary/recursive fatal termination, and the 64-report
Check budget with final suppression notice. No call-site statics. Clear only
state acquired by the current Check; recursive Check must preserve its outer
owner. Fatal bypasses the budget and never releases/reuses its primary packet.
Use owned, separately published minimal/complete regions from the outset;
leave enrichment explicitly unavailable. Check records are stack-owned and
consumed synchronously. Do not add an unsafe interim shared reusable packet.

Implement bounded plain output and Linux native thread ID/debugger query,
inspection breakpoint and termination. A debugger continue reaches termination
for fatal, false for Check. No debugger means no inspection trap. Normal fatal
uses the specified native abort path; compromised/recursive paths use immediate
exit. Text limits remain the design's 512-byte minimal and 2,048-byte complete
report, including truncation indication. Fixed buffers are not a memory-validity
guarantee for a bad caller pointer. Runtime helpers never assert internally.

Initially allow the design's explicitly documented best-effort stderr fallback;
do not label it time-bounded or durable. Even a bounded `write` can block on a
terminal/pipe. M1 is therefore a testable foundation, not a production deployment
qualification. Do not implement Windows/macOS backends that cannot be tested;
unsupported backend selection must fail clearly.

**Tests:** exactly-once/zero evaluation, explicit-bool condition, dangling else,
comma/nested-macro expression cases, no-message/literal variants, valid constexpr
success and rejected enabled constexpr failure, disabled undeclared arguments,
and changing Check results after budget exhaustion. Conditions still run after
report suppression; suppressed messages do not. Base-only binaries assert
before any initialization and during shutdown. A helper caller verifies recorded
site/expression rather than the runtime's source line.

Native subprocess death cases cover each fatal API, no atexit/destructor cleanup,
recursion from diagnostic evaluation, nested Check, secondary fatal and budget
exhaustion followed by fatal. A watchdog kills a hung child and fails the test;
parent drains bounded output concurrently. Do not fork a multithreaded Catch2
process and then run arbitrary library code in the child: launch a fresh child
executable. Disable unwanted core-file production in automated test children.
Use ASan/UBSan to test valid bounded inputs, not intentional invalid pointer
dereferences disguised as parser tests.

**Gates:** all flavor-policy fixtures, Linux native death tests, ASan/UBSan,
installed Base-only consumer, no upward link/includes, and allocation probes
in isolated binaries. Inspect passing optimized assembly: condition/branch
only; no runtime calls, TLS, atomics, locks, polling, clock or formatting.
Disabled optimized objects contain neither report calls nor unique diagnostic
strings. Compare header include trees and the plain compile budget now. A fake
backend may record break/terminate order but its termination primitive must
still terminate; it cannot return from a production `[[noreturn]]` function.

**Non-goals/deferred:** formatting, normal Logging calls, shared logging writer
migration, hook tables, stack walking, source-location STL wrappers, dialogs,
ignore registry, or an engine-wide assertion sweep.

### M2 — fault, concurrency and transport qualification

**Likely files:** existing new Base runtime/private backend/record files and
tests, its CMake test registration, and the narrow output bootstrap declaration
if a dedicated endpoint is supplied; new `tests/assert_concurrency_tests.cpp`,
`tests/assert_transport_tests.cpp`, private test backend helpers under Base
tests; `tests/assertions/` child/watchdog scripts; CI test invocation.

**Behavior:** harden the existing M1 protocol instead of redesigning it. Verify
acquire/release publication and immutable regions; add explicit delivery/error
status. No live crash reader may examine an unpublished region. If configured,
use a dedicated startup-owned, fixed-lifetime nonblocking output endpoint; no
dynamic sink registration framework or changing shared stderr flags on failure.
Without such an endpoint, document the stderr fallback's weaker guarantee.
Specify short write, EINTR, EAGAIN, closure and bounded retry behavior. Treat
output loss as lost diagnostics, never as permission to resume a fatal.

**Tests/gates:** barrier-start failures from worker/render-like threads while
holding application locks; Check racing Check/fatal; reentrant messages; fatal
while a Check owns the slot; constant TLS first use; allocation interception;
budget saturation; first packet preserved when a secondary terminates early.
Fault-inject each publication boundary with a private test seam and validate
that readers only see complete stages. Full/closed/nonreading transport tests
must finish under a watchdog. A failed Check must not accidentally die from
SIGPIPE when output is a closed pipe. Choose and test a backend-local solution;
do not change the application's process-wide signal policy silently.

Run TSan on nonfatal ownership/publication scenarios in a separate tree and
ASan/UBSan on bounds/lifetime cases; native death tests remain separate, with
explicit expected termination statuses. Run real GDB or LLDB attach, continue
and detach cases: report/site inspectable, fatal never resumes normal execution,
Check break obeys flavor, unavailable `/proc` degrades to no debugger detected.
Record ptrace/host restrictions as unverified coverage, not passes.

**Non-goals/deferred:** async logger, main-thread rendezvous, arbitrary
callbacks, a signal-handler-safe assertion API, collector implementation, stack
unwinder, or “all simultaneous failures are reported.” M2 can qualify a specified
Linux deployment; it cannot guarantee progress through arbitrary OS/stdio stalls.

### M3 — opt-in diagnostic formatting

**Likely files:** new Base public `assert_format.hpp`,
`src/diagnostic_format.cpp`, private record declarations as needed, Base CMake,
new `tests/assert_format_tests.cpp` and parser fuzz driver,
`tests/assertions/` compile-fail/lifetime fixtures, ADR 0003, SDK consumer,
focused measurement script under `tests/assertions/`.

**Behavior:** only the closed scalar/boolean/text/object-pointer argument set
and up to eight arguments from the design. Add the four `_F` APIs with `{}` and
brace escapes. Normalize tags through tiny overloads/pack adapter; all parsing,
integer/float conversion and bounded writing live in the `.cpp`. Construct and
evaluate arguments only after Begin succeeds. Never expose `std::format`,
`std::string`, C varargs, parser templates or arbitrary user formatters.

Keep copied values and borrowed text valid through synchronous Finish; retained
reports own their bytes. Explicit counted text handles non-NUL data. Deleted
or constrained conversion paths must prevent function/member pointers and
unsupported values from silently becoming boolean arguments. Account for alias
overlaps (`usize`, fixed-width integers) without duplicate overloads. LP64 is
tested here; LLP64 remains a port gate until a real toolchain is available.

**Tests/gates:** every supported width/extreme, null/long/counted/control-byte
text, escaped braces, missing/extra arguments, malformed formats, exact buffer
boundaries, zero/eight/nine arguments, unsupported type/pointer compile failures,
temporary lifetimes, and recursion during argument evaluation. ASan/UBSan fuzz
uses bounded readable memory. Formatting errors return a marked bounded report,
never assert, allocate, throw or overread. Intercept allocation on first-use
integer and floating conversion independently; if the supported implementation
fails the gate, defer float support or supply a separately justified converter.
Do not assert allocation/signal-safety guarantees from `to_chars` alone.

Run the full formatted compile/binary measurements before spreading `_F` calls;
parser/converter implementations must occur once, outside consumer templates.
Keep the format header below the design's 300-non-comment-line target, with
no additional STL headers beyond the plain assertion include graph.
Install and compile the opt-in header independently. Plain-header include and
timing gates must remain unchanged. Run all policy/death regressions.

**Non-goals/deferred:** general formatting grammar, consteval format parser,
string library, named arguments, user customization points, and printf support.

### M4 — narrow integration, useful reports and measurement evidence

**Likely files:** `modules/foundation/logging/src/emergency_logger.cpp`,
Logging CMake/tests as needed, Base `diagnostic_output.hpp/.cpp`,
`modules/foundation/base/include/ludus/foundation/base/pointer.hpp` (remove only
the invalid null-deletion TODO), selected audited invariant call sites,
SDK fixtures, CI, `docs/development/build-profiling.md`, and a new measured
assertion validation report under `docs/architecture/`.

**Behavior:** Logging's private emergency record builder uses the shared final
Base byte boundary. No Base include or link upward; assertions remain usable
with Logging absent, before initialization, after shutdown, and from a logging
sink failure. Logging's ordinary Fatal behavior stays returning. If sharing
output would change a non-Linux path that cannot be tested, retain that path
and document the port limitation instead of claiming tested parity.

Audit any new call site for a programmer invariant and absence of necessary
side effects. Check sites must have an explicit recovery branch. Null deletion,
bad external data, file/network errors and GPU/device loss are not assertions.
No minimum migration count: skip sites without a real invariant.

**Tests/gates:** logging lifecycle regression tests and new assertion integration
children; no recursion through Logging, no allocation in approved assertion
path, recovery intact, no SDK private-header leak. Re-run configuration, native
fatal, sanitizer and debugger gates. Measure plain/format headers and final
binary after integration; verify touching only a runtime `.cpp` recompiles that
object and relinks, without recompiling consumers.

Produce a copyable bounded report identifying format version, build/flavor/policy,
native thread, site/expression and truncation/capture status. Current git revision
metadata alone is not an exact executable/symbol identifier, particularly for
dirty or differently configured builds. Validate an artifact ID/symbol mapping
in the deployment or explicitly mark exact symbol matching unavailable.
With a healthy launcher retaining output or configured OS crash artifacts,
trigger the same helper from two callers with no debugger, retrieve the report,
and verify caller symbolication where capture exists. Missing stack/capture
must be visible; an ephemeral stderr line is not a QA handoff pass.

**Non-goals/deferred:** integrating unsafe `LogSystem::Flush`, migrating unrelated
logging utilities, silently enabling global frame pointers, building a collector
or claiming persistence without a retained-output deployment.

### M5 — richer diagnostics only after their prerequisites exist

**Entry gate:** a concrete process-lifetime nonblocking logger/collector adapter,
symbol/build identity pipeline, or supported native port exists and can be
tested. None currently does. This milestone is optional and does not block
M1–M4's independent diagnostics.

**Conditional files:** rare public `diagnostic_hooks.hpp`, Logging's private
`diagnostic_bridge.cpp`, private Base platform/packet files, adapter tests;
Windows/macOS source files only with corresponding native test infrastructure.
Do not create empty frameworks or pretend implementations in M0–M4.

**Behavior/gates:** immutable versioned bootstrap-only hook table, preallocated
copy/notification, independent emergency report first, optional bounded mirror
and flush ticket. Test full queues, failed initialization, shutdown/lifetime
rules, reentrancy, stalled consumer, owned record lifetime and deadline behavior.
Raw PCs only if the platform unwinder passes bounds/allocation/lock audits;
symbolication remains offline. New ports require native debugger and termination
tests plus LP64/LLP64/header/build qualification. A future DSO/plugin deployment
must first establish one runtime owner across the process; static copies in
multiple plugins are not acceptable.

**Non-goals:** generic handlers/formatters, resumable fatal failures, UI, upload,
global thread suspension, and speculative platform support.

## 5. Validation matrix and execution procedure

These are **future acceptance gates**, not results from this planning change.

| Gate | Debug | Development | Profile | Release / MinSizeRel | Sanitizers | First required |
| --- | --- | --- | --- | --- | --- | --- |
| Engine/test exception flags and generated commands | Yes | Yes | Yes | Engine; test-enabled Release tree | ASan/UBSan | M0a |
| Flavor/config/header/manifest agreement and negative fixtures | Yes | Yes | Yes | Both policies `0/0` | Development policy | M0b |
| Install/export + consumer with SDK policy independent of client `NDEBUG` | Yes | Yes | Yes | Release SDK; MinSizeRel config probe | Explicit matching link flags if exercised | M0b |
| Plain macro evaluation, constexpr/compile-fail fixtures | Yes | Yes | Disabled Assert; active Require/Check/Fatal | Same as Profile | ASan/UBSan | M1 |
| Native fatal/no-resume/no-cleanup and Check recovery | Yes | Yes | Yes | Test-enabled Release; MinSizeRel compile/codegen | Bounds runs separate from expected deaths | M1 |
| Recursion, owner contention, packet publication, suppression budget | Yes | Yes | Yes | Release runtime | ASan/UBSan + separate TSan run | M1 basic, M2 full |
| Fake and native debugger/detach policy | Check break on | Check break on | Check break off | Check break off | Native unsanitized baseline | M2 |
| Transport fault/liveness and first-use allocation | Yes | Yes | Yes | Yes | ASan/UBSan; valid memory inputs | M2 |
| Typed packing/format correctness/lifetimes | Yes | Yes | Disabled Assert_F too | Test-enabled Release | Parser ASan/UBSan fuzz | M3 |
| Assembly, disabled strings, include graph, compile/binary budgets | Header probes | Full measured baseline | Optimized code | Optimized code; LTO probe | Sanitizer overhead excluded from timing comparison | M1 plain, M3 formatted, M4 final |
| Logging lifecycle + QA artifact retrieval | Yes | Yes | Yes | Shipping-like deployment | Integration ASan/UBSan | M4 |

For production changes, preserve the standard warning-clean build/test,
format/tidy, sanitizer and installed SDK gates. Use the managed tools, and
refresh bootstrap after modifying presets because they are bootstrap inputs:

```bash
./init.sh --no-system-install
./scripts/doctor
CI=true ./scripts/build linux-clang-debug
CI=true ./scripts/test linux-clang-debug
CI=true ./scripts/build linux-clang-development
CI=true ./scripts/test linux-clang-development
CI=true ./scripts/build linux-clang-profile
CI=true ./scripts/test linux-clang-profile
CI=true ./scripts/build linux-clang-release
CI=true ./scripts/check linux-clang-development --all
CI=true ./scripts/build linux-clang-asan-ubsan
CI=true ./scripts/test linux-clang-asan-ubsan
./scripts/install-sdk linux-clang-debug
./scripts/install-sdk linux-clang-development
./scripts/install-sdk linux-clang-profile
./scripts/install-sdk linux-clang-release
```

Release tests need their own configured tree because the production preset
switches tests off on every configure. For example, after bootstrap:

```bash
out/host-tools/venv/bin/cmake --preset linux-clang-release \
  -B /tmp/ludus-assert-release-tests \
  -DLUDUS_BUILD_TESTS=ON -DLUDUS_WARNINGS_AS_ERRORS=ON
out/host-tools/venv/bin/cmake --build /tmp/ludus-assert-release-tests
out/host-tools/venv/bin/ctest --test-dir /tmp/ludus-assert-release-tests \
  --output-on-failure
```

Add a separate TSan tree using the Development preset with
`-DLUDUS_ENABLE_TSAN=ON -DLUDUS_ENABLE_ASAN=OFF -DLUDUS_ENABLE_UBSAN=OFF`;
run the new `assert-concurrency` CTest label there. Add `assert-death`,
`assert-format`, and `build-contract` labels when their fixtures exist, while
keeping an unfiltered run as a completion gate. Set timeouts on child tests;
an expected signal exit is a test assertion, not a blanket suppression of
sanitizer diagnostics. If the host cannot run TSan/debugger tests, record the
restriction and require a supported runner before claiming that gate complete.

For MinSizeRel, configure a separate compiler/policy fixture or a tests-off
engine tree with Release flavor and matching Conan configuration. Do not just
reuse a RelWithDebInfo package and claim full MinSizeRel SDK qualification.
Pass CMake `-D` options to configure directly; `scripts/build` forwards trailing
arguments to the **build** command, not configuration.

For measurement, register a small assertion-only fixture generator/runner under
`tests/assertions/`, not another engine framework. Reproduce the design's
0/100/1,000/10,000-site cases and handwritten baselines with Clang 18, caches
disabled, five runs, median and variance. Include empty-header, literal,
repeated-type and diverse-type formatted cases. Record frontend/backend time,
peak RSS, instantiation traces, `.text`/`.rodata`, linked sections and optional
LTO results. Required targets remain: at most 10 ms empty-header frontend delta,
at most 15% overhead at 1,000 plain sites, at most 25% for formatted packing;
report uncertainty if host noise exceeds the absolute target. Inspect include
trees even when no header appears in ClangBuildAnalyzer's expensive list.

Run `./scripts/check-build-budget --profile` for fresh engine-wide evidence in
the designated disposable profiling build tree; it currently rebuilds
`linux-clang-development`. Archive before/after reports before running again.
Do not raise the current budgets to make a regression disappear. Runtime `.cpp`
touches, plain-header touches, format-header touches and generated-policy
changes must each show the expected distinct rebuild set. Parser changes should
not recompile assertion consumers; policy changes deliberately should.

## 6. Risks and unresolved gates

| Risk | Required control / remaining decision |
| --- | --- |
| Condition evaluated twice or effect disappears in Shipping | One contextual-bool evaluation in enabled macros; no arguments in disabled expansion. Never place required operations inside optional Assert. |
| Message evaluated before recursion protection | Begin precedes argument evaluation in both plain and `_F` forms. Test a diagnostic expression that itself asserts. |
| Per-TU policy/ODR mismatch | Generated non-overridable variant header, consistent public inline definitions, package identity tests, no `NDEBUG` inference. Do not imitate Logging tests' per-TU log-level override for assertions. |
| Profile/configuration confusion | Explicit flavor separate from CMake/Conan configuration. Verify actual commands and exported SDK, not names/comments. |
| Test harness accidentally masks production termination | Fresh native child executable links real Base; no throwing across `noexcept`, returning noreturn fake, or production “test mode” that continues. |
| Hidden header/template cost | Narrow include closure, no runtime metadata header in `assert.hpp`, trace/assembly tests and distinct formatted opt-in. |
| Logging recursion/upward dependency | Assertions never dispatch/flush Logging; shared final writer is below it. No logger TLS initializer. |
| Dynamic TLS initialization or hidden allocation | Trivial `constinit` TLS in runtime only; inspect generated code and intercept allocation on first-ever thread failure. Avoid function-local dynamic singleton initialization. |
| Concurrent/recursive state corruption | Nonwaiting owner, recursive Check leaves outer state intact, secondary fatal exits, immutable staged packet. No guarantee of complete simultaneous reports. |
| Check budget changes program behavior | Budget only suppresses presentation. Every condition still runs once; false still returns false; fatal bypasses budget. |
| Debugger resumes or detaches between detection and trap | No resume branch after fatal breakpoint; race is platform-specific, tested/documented. Detection failure skips inspection. Never replace terminal path with only a debug trap. |
| Borrowed formatted text dies too soon | Synchronous packing/Finish and owned retained bytes; ASan temporary/Check-return tests. No saved varargs or deferred user closures. |
| Pointer silently converts to bool / invalid text pointer | Closed overload set and compile-fail tests. Read bounds do not make dangling/nonmapped memory safe; minimal site is already committed. |
| Disabled code retains report strings/runtime calls | Optimized object/symbol/string checks with unique sentinels, with and without LTO; no assume/unreachable substitutions. |
| Output blocks or sends SIGPIPE | M1 fallback explicitly weaker. M2 must choose/test a fixed-lifetime nonblocking transport and safe closed-endpoint behavior, without global signal-policy side effects. |
| Sanitizers alter death status or cannot initialize on host | Separate native termination evidence from sanitizer tests; inspect failure cause and report infrastructure limits. Never label an unrun gate passed. |
| SDK archive/header mismatch or wrong install configuration | Separate prefixes, pre-overwrite manifest check, installed multi-TU policy/runtime tests. Multi-config support deliberately unavailable until correctly packaged. |
| Duplicate runtime state in linked modules | Current one-executable static deployment only. Before plugins, decide owning binary/export/import boundary and test one owner across modules. |
| No exact build ID or retained report | Runtime can report current revision/flavor; exact symbol identity and retention require deployment evidence in M4. Do not equate git hash with executable ID. |
| Abort/core behavior depends on environment | Native subprocess tests; record core policy, signal-handler caveats and crash artifact availability. Immediate exit trades capture for a simpler compromised path. |

The open decisions are bounded: M2's Linux nonblocking endpoint and closed-pipe
policy; M3's demonstrated floating conversion costs; M4's deployed
artifact/symbol retention mechanism.
They do not justify new infrastructure before its milestone. Windows/macOS,
async flush adapters and crash collectors remain deferred prerequisites, not
unanswered blockers for Linux plain assertions.

## 7. Exact first change and validation of this plan

**Next implementation change: M0a only.** Fix the effective test exception
policy in `EngineOptions.cmake`/`EngineTargets.cmake`, add a regression probe
that uses actual target compilation options, and pass M0a's gates. Do not add
assertion macros in that patch. Follow with M0b as a separate configuration/SDK
change; only then begin M1. This order removes a verified test-infrastructure
defect before using it to qualify fatal infrastructure.

No tiny prerequisite was applied during planning. Even the small flag-order
fix deserves its own generated-command and test validation; folding it into a
documentation review would obscure the distinction between findings and fixes.

Inspection commands/results for this planning change:

- `./scripts/doctor`: passed. Found LLVM/Clang/format/tidy 18.1.3, managed CMake
  3.29.6, Ninja 1.11.1.3 and Conan 2.8.1. GDB is available; LLDB and ccache are
  optional and unavailable. No native debugger test was run.
- `out/host-tools/venv/bin/cmake --preset linux-clang-profile -B /tmp/ludus-assert-plan-profile -DLUDUS_PLATFORM_ENABLE_WAYLAND=OFF --graphviz=/tmp/ludus-assert-plan-profile.dot`:
  passed. Only configured/generated an isolated headless tree; examined its
  compilation database, export file and target graph.
- Temporary compiler syntax probes using that compilation database: engine
  exception construct rejected as required; test exception construct also
  rejected, exposing the defect; the same test probe with final `-fexceptions`
  passed. These are inspection probes, not repository tests or code changes.
- Repository source/include/name searches and the commit delta confirmed the
  findings above. No engine build, unit suite, sanitizer suite, SDK install or
  performance benchmark is claimed for this documentation-only change.

Document checks passed: all 15 local links resolve, the three fenced blocks
are balanced, and no trailing whitespace or tabs were found. `git diff --check`
passed; the new document was also inspected with
`git diff --no-index -- /dev/null docs/architecture/assertions-implementation-plan.md`
(exit 1 means the expected added-file diff). Repository status and a before/after
content-hash comparison show this work adds only the plan. A concurrently
edited logging review was committed by separate work and was left untouched.
Engine format/tidy tools do not check Markdown; no engine checks are claimed
for this document change.
