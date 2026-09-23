# Interactive development assertions: implementation plan and Kiro prompts

Status: proposed replacement for the development-ASSERT policy, not implemented.
Prepared against the current merged repository on 2026-09-23. This plan changes
specific decisions in `assertions.md`; it does not claim the existing fatal
contract already permits continuation. Use the prompts at the end in order.

## Decision to make explicit

A development `ASSERT` should stop for inspection and allow a developer to
explicitly continue. An always-on `REQUIRE` or `FATAL` still means execution must
not resume. Do not merely remove `abort()`, convert ASSERT into CHECK, or make
assertions silently log and continue.

User-confirmed local Debug no-debugger experience: a separate helper displays a
report with **Continue once** and **Terminate** actions. This is the requested
replacement behavior; it is not implemented yet.
Continue once resumes after the macro, without re-evaluating the condition or
message. It does not repair state, validate a recovery path, or ignore that site
on subsequent visits. Conditions whose violation makes subsequent execution
unsafe regardless of developer preference belong in `REQUIRE`.

| API | Proposed failure action | Can return after failure? |
| --- | --- | --- |
| `ASSERT`, `ASSERT_F`, enabled | Report; local debugger inspection, eligible Debug dialog, or noninteractive termination according to the matrix below | Only through an eligible explicit developer action |
| `ASSERT`, `ASSERT_F`, disabled | Disappear completely, as today | No evaluation occurs |
| `REQUIRE`, `REQUIRE_F` | Report; optional inspection break; terminate even after debugger Continue | No |
| `FATAL`, `FATAL_F` | Same terminal behavior, unconditionally | No |
| `CHECK`, `CHECK_F` | Best-effort visible report; existing configured debugger break; return false | Yes; caller must handle false |

CHECK does not open an interactive dialog or turn into a fatal assertion. Its
existing reporting budget and suppression rules remain. A standalone CHECK
statement is still misuse when subsequent work depends on the failed condition.
No new macro names or general handler API are needed.

### Build and execution policy — mandatory

Dialogs are available **only in Debug builds running outside CI**. Development
is a distinct Ludus build flavor and must not be treated as Debug merely because
it has debug symbols or assertions enabled.

| Execution | Failed enabled ASSERT | Dialog permitted? |
| --- | --- | --- |
| Local Debug, debugger attached | Report, break; debugger Continue resumes | Eligible, but bypassed in favor of debugger |
| Local Debug, no debugger, interactive session | Report, Continue once / Terminate dialog | Yes |
| Local Debug, explicitly noninteractive/headless | Report and terminate | No |
| Local Development, debugger attached | Report, break; debugger Continue resumes | No |
| Local Development, no debugger | Report and terminate | No |
| CI, any build flavor | No interaction or intentional debugger stop; enabled ASSERT terminates, disabled ASSERT disappears | No |
| Profile/Release with current policy | ASSERT is compiled out; REQUIRE/FATAL still terminate and CHECK returns its result | No |

Enforce this at two levels. Generate a SDK-owned numeric
`LUDUS_ASSERT_DIALOGS_AVAILABLE` capability from the explicit build flavor and
CI build setting: it can be 1 only for non-CI Debug. Reject requests to enable it
for other flavors or CI; do not infer it from NDEBUG or allow per-TU overrides.
Export the capability consistently with the runtime and SDK manifest. It
controls presentation availability, not whether conditions are evaluated.

Also apply a startup-only runtime veto: a locally built dialog-capable Debug
binary must not prompt when subsequently run in CI or noninteractive mode.
Use an explicit CI configuration supplied by repository workflows, default it
from the CI environment, and recognize the supported runner markers at startup.
Define and test the environment-value rules. Environment detection alone cannot
identify every possible CI system; documented CI invocation must explicitly
select noninteractive mode. CI takes precedence over any request to interact.
Cache the policy before worker threads start; never read environment variables
on assertion success paths. The helper independently rejects forbidden UI mode.

CI and Development startup must not require Zenity, a display, stdin input, or
the interactive control channel. They retain report-only diagnostic capture.
No CI run waits for a dialog or breakpoint, even if a debugger or graphical
display happens to be present. Suppress CHECK inspection breaks in CI as well;
its false result and reporting budget remain unchanged.

## Verified repository facts

- Both ASSERT forms currently call `BeginFatal` and a nonreturning fatal finish.
  REQUIRE shares that path. Returning from the debugger still reaches `abort()`.
- `FinishCheckImpl` reports through `TryWriteEmergencyBytes`, optionally breaks
  when `LUDUS_BREAK_ON_CHECK` and debugger attachment both permit it, then returns
  false and releases reporting ownership.
- Base has constant-initialized TLS, one atomic reporting owner, and an owned
  fatal packet with release/acquire publication. The packet is never reused.
- The datagram transport is configured through `ConfigureEmergencySocket`.
  Without it, assertion presentation can be absent. Normal Logging initialization
  does not configure that transport.
- `apps/smoke/main.cpp` currently has no diagnostic-session setup. It contains
  the user's uncommitted CHECK experiment; preserve that edit.
- `diagnostic.hpp` is the separate Base emergency-report API used by Logging.
  Its byte writer can fall back to blocking stderr. Assertions must not acquire
  that fallback or call normal Logging.
- Existing native/fake death tests, allocation probes, sanitizer trees, SDK
  consumers, and header/build-cost checks provide the starting validation suite.
- Linux/Clang is the validated engine backend. The local machine has a `zenity`
  executable, but that alone does not validate a graphical session or its UI.
- Preserve the unrelated `.gitignore` edit as well.

Relevant sources include:

- `modules/foundation/base/include/ludus/foundation/base/{assert.hpp,assert_format.hpp,diagnostic_output.hpp}`
- `modules/foundation/base/src/{assert.cpp,diagnostic_format.cpp,diagnostics_linux.cpp,diagnostic_output.cpp}`
- `modules/foundation/base/src/internal/{diagnostic_finish.hpp,diagnostic_record.hpp,diagnostic_platform.hpp}`
- `cmake/assert_config.hpp.in`, Base's CMakeLists, SDK manifest/config templates
- `modules/foundation/base/tests/assert_child.cpp`, `assert_platform_fake.cpp`,
  and the other assertion tests; `tests/assertions/` and `tests/build_contract/`
- `modules/foundation/logging/tests/assert_lifetime_child.cpp`
- `apps/smoke/main.cpp`, its CMakeLists, and `scripts/python/engine.py`

## Architecture

### 1. Separate resumable and terminal entry/finish functions

Keep thin macros and Begin-before-message-evaluation. Introduce private
`BeginAssert` / `FinishAssert` and corresponding formatted/rendered finish paths.
The precise internal signature can follow current conventions, but ASSERT must
no longer enter a function declared `[[noreturn]]`. Fatal entry/finish stays
nonreturning at the terminal boundary. Do not weaken REQUIRE to enable ASSERT.

An enabled passing assertion remains just condition evaluation and a cold
branch. No success-path helper call, TLS access, atomic, lock, clock, debugger
query, allocation, or per-site state. Disabled ASSERT arguments still disappear,
including nonexistent identifiers and unique diagnostic strings.

A normal first ASSERT failure owns the report slot while composing and presenting
its incident. On Continue, clear its TLS entry and release the slot exactly once.
The next failed ASSERT must be independently reportable. Keep optional message
and format arguments inside the guarded failure branch.

Use a bounded, stack-owned development report for resumable ASSERT. Do not put it
in `gFatalPacket`, clear that packet's ready flags, or reuse terminal packet
storage. The debugger can inspect the live report while stopped in the cold
finish function; verify this with the optimized Development build. A helper
receives its own copy before the stack record can go out of scope. If the user
chooses Terminate, publish terminal evidence from the already-owned incident
without re-evaluating any diagnostic expression, then terminate.

The existing fatal packet protocol remains terminal and immutable after
publication. Keep its version distinct from any new IPC protocol version.

### 2. Put presentation outside the engine process

Use a small Linux development helper, not the engine window manager, render
thread, main-thread queue, normal logger, or a dynamically registered handler
chain. The failing thread may hold any of their locks.

A concrete first adapter is a Python development helper under
`tools/diagnostics/`, using the standard library for IPC/process management and
Zenity for the graphical dialog. This is a proposed development-tool dependency,
not a dependency of FoundationBase or its public headers. Validate the installed
Zenity interface and real Wayland operation before selecting exact command-line
options. Invoke it with an argument vector, never a shell command assembled from
diagnostic text. Render diagnostic text literally, without interpreting markup.
If the adapter cannot meet the interaction contract, document that finding before
substituting another tool; do not add a UI toolkit to Base for convenience.

Reuse the current connected datagram endpoint for ordinary report delivery.
Add a separate, startup-configured connected control endpoint for the one active
interactive ASSERT. A Linux `SOCK_SEQPACKET` socketpair is the recommended control
transport: report traffic must not consume or masquerade as decision replies.

The control protocol is small and versioned, with an incident ID, explicit
message kind/length, bounded owned report bytes, and only ContinueOnce/Terminate
responses. Specify byte encoding instead of sending compiler-padded C++ structs.
Validate lengths, versions, IDs and action values. Unknown, stale, duplicate, or
malformed replies never authorize continuation. Only the active incident's
explicit response can do that. No arbitrary commands, callbacks or remote server.

The helper must continue draining reports while its dialog is open. CHECKs and
fatal reports are displayed/recorded but never wait for user acknowledgement.
The interactive request carries its own complete report, so loss of a separate
diagnostic datagram cannot produce an empty dialog. Do not wait for a helper to
acknowledge a fatal report before terminating the engine.

### 3. Make normal development launches work

Healthy startup creates/configures endpoints, starts the helper, validates the
handshake, and transfers only the required descriptors before worker threads
start. Never spawn, fork, locate Python, open a dialog, or allocate transport from
inside assertion failure handling.

Add a narrow startup-only development integration target under
`tools/diagnostics/` that depends on Base. Link it into the smoke application for
Debug/Development, with report-only mode for Development and CI. Only eligible
local Debug starts the graphical adapter. Base must not depend on this target
or on Platform/Logging.
Use a separate startup header, not `assert.hpp`. Arrange build-tree helper paths
and installation deliberately; do not embed this checkout's absolute path in an
installed SDK. No generic Diagnostics manager or framework is needed. The startup initializer
returns an explicit status; healthy startup errors can use the existing Base
emergency facility. No C++ exceptions enter engine integration code.

**The direct executable launch shown in the user's screenshots must work**, not
only a specially remembered wrapper command. The smoke app's normal Debug or
Development startup must establish a diagnostic session before ordinary engine
initialization, unless one was already provided by an approved launcher.
Optionally expose the same setup through a small `scripts/run` launcher for
other binaries and SDK examples.

If eligible local Debug requests interactive setup but it is unavailable
(missing helper/UI/display), fail healthy
startup with an explanatory error and a documented noninteractive option. Do not
start in a mode that silently loses reports and surprises the developer later.
CI is automatically forced into noninteractive behavior, also set explicitly
by repository workflows: display/capture reports, fail a failed enabled ASSERT,
and never wait for input. Development is report-only without a GUI dependency.
Headless local Debug can explicitly select noninteractive mode. A direct API user
who skips startup receives no magical UI guarantee; document the fallback.

Startup belongs in the application/tool integration, not a hidden static
initializer. Before-startup failure and corrupt-state failure cannot promise the
ordinary interactive experience. Keep runtime descriptors valid for process
lifetime so post-logger-shutdown diagnostics still work; do not race their close
with failures. The helper detects child exit and closes any pending dialog.

### 4. State the limits and fallback policy honestly

Waiting for a developer is intentional in the resumable ASSERT path. It is a
specific relaxation of the old no-wait contract, not a claim of bounded failure
completion. REQUIRE/FATAL and CHECK retain their current nonwaiting actions.
The failing thread pauses; other engine threads are not automatically suspended.
A debugger may stop all threads using its own policy. Do not describe a helper
dialog as an atomic snapshot of the whole engine.

Outside CI and explicit noninteractive mode, with a debugger attached, ASSERT
breaks directly. Debugger Continue resumes past
ASSERT; there is no second dialog and no subsequent unconditional abort. REQUIRE
and FATAL still abort after that same debugger action. Do not emit an unconditional
trap without a detected debugger. Test and document the attach/detach race.

Only eligible local Debug without a debugger waits for an explicit helper
decision. Development without a debugger and CI report and terminate instead.
Do not automatically continue because a dialog closes, input is malformed, the
helper disconnects, or a timeout elapses. Dialog cancellation, helper loss, and
unavailable interaction fall back to terminal failure. No automatic short timeout
while a healthy dialog is awaiting a human; a hung helper can require external
termination. That is an explicit limitation of this small first implementation,
not permission to add a heartbeat service or general watchdog framework.

Retain the current compromised-path policy for recursion and reporting contention:
recursive ASSERT/REQUIRE/FATAL and a concurrent ASSERT/REQUIRE/FATAL while another
incident owns the slot terminate through the minimal path. They do not silently
resume, wait for application locks, or open nested dialogs. Recursive/concurrent
CHECK preserves the outer state and returns false without message evaluation.
Thus the first ordinary development ASSERT is resumable; catastrophic or
simultaneous incidents are not guaranteed an interactive decision. Make this
exception prominent in the public contract and tests. Supporting multiple pending
interactive incidents is a separate design, not an unbounded queue in this patch.

A Continue button means developer-authorized experimentation with a violated
invariant, not general recovery. Do not use ASSERT as a memory-safety prerequisite
and then assume its failed condition became true after Continue. Audit actual
ASSERT call sites; use REQUIRE where continuation is forbidden, and normal
status/control flow for external data and expected operational failures.

## Contract changes and exclusions

Amend the assertion design's taxonomy, build-policy table, macro pseudocode,
concurrency rules, debugger behavior, and acceptance tests. Update the historical
implementation-plan references with a clear superseding note rather than
pretending earlier fatal-only tests were wrong. Add a small ADR recording this
user-directed policy change.

Bump the generated assertion policy version because old headers/runtime assume
ASSERT is terminal. Update manifest/consumer policy checks and document matching
SDK header/library installation. Do not derive the policy from NDEBUG or permit
per-TU overrides. Runtime interaction mode is startup-owned; it does not change
which assertions are compiled in.

Do not add Ignore Always, site registries, mutable macro statics, generalized
formatters, crash uploads, stack symbolication, Windows/macOS production
backends, or logger flushing on assertion entry. Do not merge the independent
Logging and assertion formatters as part of this work.

## Kiro prompts — execute sequentially

### Prompt 1: amend the contract before changing code

```text
Work in the Ludus repository. Read AGENTS.md, .kiro/steering/coding-standards.md,
the relevant ADRs, docs/architecture/assertions.md, its implementation plan and
adversarial review, and docs/architecture/assertions-interactive-development-plan.md.
Inspect the current runtime, configuration, and tests before making assumptions.

Implement only the documentation/spec milestone of the interactive-development
plan. The requested change is intentional: enabled ASSERT/ASSERT_F becomes
resumable through explicit developer action; REQUIRE/FATAL remain terminal;
CHECK remains recoverable. The user explicitly selected a separate dialog with
Continue once/Terminate when no debugger is attached, exclusively in local
non-CI Debug builds. Encode the full build/execution matrix, including a runtime
CI veto for locally built Debug binaries and report-only Development behavior.

Update the authoritative design and add a short ADR and a Kiro spec with precise
requirements, design, and tasks. Resolve conflicts with the old fatal-only ASSERT
contract explicitly. Include direct-executable startup, headless behavior,
recursion/contention, transport/helper loss, debugger continuation, owned-report
lifetimes, SDK policy versioning, and all validation gates from the plan.

Preserve existing uncommitted changes, especially .gitignore and the user's
apps/smoke/main.cpp experiment. Do not implement runtime/UI changes yet. Report
actual open decisions without turning routine implementation choices into
permission requests. Leave concrete acceptance tests for the next prompts.
```

### Prompt 2: establish visible diagnostics and healthy startup

```text
Implement the startup/report-delivery milestone of
docs/architecture/assertions-interactive-development-plan.md and its reviewed
Kiro spec. Read repository rules and the current implementation first.

Build the small external Linux diagnostic helper and startup-only integration.
Reuse the existing nonblocking datagram report transport. Add the separate,
versioned control endpoint/handshake needed for a later explicit ASSERT decision.
No runtime handler framework. Keep FoundationBase below Logging/Platform.

Make normal direct Debug/Development smoke launches initialize diagnostics before
engine workers and logger setup. Allow graphical setup only in local non-CI
Debug; Development and CI use report-only mode and require no display/Zenity.
Automatically force CI noninteractive and set that policy explicitly in workflow
commands too. Local headless Debug can opt into noninteractive mode. Preserve the
user's application edits. Resolve build/install paths and descriptor ownership;
never launch helper/UI code from a failure handler. Validate graphical capability
at eligible interactive startup rather than equating a found executable with
working presentation. Report failed interactive setup visibly.

Do not change ASSERT's fatal action in this milestone. Prove that ASSERT/FATAL
reports, CHECK reports, and formatted messages are visible/captured independently
of normal Logging, including pre-init/post-shutdown and stderr-closed tests.
Verify helper cleanup on child exit, missing-helper/headless behavior, malformed
handshakes, and bounded core transport. Do not fake native death behavior.
Run focused tests, pinned format/tidy, and SDK/configuration checks. Record exactly
what was tested. Keep all production assertion headers lightweight.
```

### Prompt 3: implement explicit resumable ASSERT and the dialog

```text
Implement the resumable-ASSERT and interactive presentation milestone from
docs/architecture/assertions-interactive-development-plan.md and its reviewed
Kiro spec, on top of the working startup/helper integration. Read repository rules.

Split ASSERT/ASSERT_F from BeginFatal/FinishFatal. Preserve Begin-before-message,
once-only evaluation, disabled-argument elimination, and zero success-path
coordination. Keep REQUIRE/FATAL nonreturning, including after debugger Continue.
Use bounded owned resumable reports without reusing the immutable fatal packet.
Release TLS/report ownership exactly once after Continue so later failures work.

Outside CI/noninteractive mode, with an attached debugger, break for ASSERT and
return after debugger Continue. Without one, only eligible local Debug may display
a dialog in the external helper and accept the active
incident's explicit Continue once or Terminate response. Continue skips that
assertion once; never retry its condition or ignore future hits. Complete the
Linux graphical adapter using the independently launched development UI tool.
Validate actual Wayland interaction; no toolkit code in Base or engine UI calls
from the failing thread. Dialog close/helper failure never means Continue.

Generate SDK-owned LUDUS_ASSERT_DIALOGS_AVAILABLE only for non-CI Debug; add the
startup runtime veto so a locally built Debug binary also cannot prompt in CI.
CI must bypass inspection breaks, including CHECK breaks. Development without a
debugger reports and terminates without UI. Preserve the documented fallback for
recursive/concurrent ASSERT incidents. CHECK stays boolean, bypasses dialogs,
and retains its reporting budget. Fatal failure never waits for the helper.
Bump the generated SDK assertion-policy version and update consistency tests.

Add subprocess tests with a controlled helper for Continue, Terminate, repeated
ASSERTs, debugger continuation, mismatched/duplicate/malformed replies, helper
exit, recursion/contention, and locks held at failure. Keep argument-evaluation,
allocation, fatal packet, disabled residue, and formatter tests. Do not remove
fatal tests: move the non-resumption expectations to the terminal API family and
retain explicit tests for headless ASSERT and compromised fallbacks. Build and
run focused tests incrementally; no test-only replacement for native termination.
```

### Prompt 4: qualify the experience and deliver the implementation

```text
Audit and complete the implementation of
docs/architecture/assertions-interactive-development-plan.md and its reviewed
Kiro spec. Read all repository rules and inspect the diff, not just test results.

Verify the exact user-facing workflows: direct local non-CI Debug smoke launch,
failed plain and formatted ASSERT, report text and expression visible, Continue
once resumes, a second failure is reported again, Terminate exits, CHECK returns
false with visible diagnostics, and REQUIRE/FATAL never resume. Run attached-
debugger tests proving ASSERT continues but REQUIRE/FATAL terminates afterward.
Exercise real Linux/Wayland UI where available; distinguish automated protocol
coverage from actual GUI validation. Keep tests isolated from the user's live app.
Verify Development never launches a dialog, including with DISPLAY/WAYLAND_DISPLAY
present. Test CI-built Debug and locally built Debug executed in CI: no GUI,
stdin prompt, or inspection breakpoint; failed enabled ASSERT terminates. Test
that an interactive request cannot override CI and that headless capture works
without Zenity. Use watchdogs to detect accidental input waits. Automated protocol
tests must not weaken the production CI gate to exercise GUI behavior.

Run Debug, Development, Profile, and test-enabled Release; ASan/UBSan and TSan;
allocation interception on core failure paths; native subprocess watchdog/death
cases; malformed/closed/saturated transport and helper-loss cases; app-lock-held
and logger-lifecycle tests; installed SDK consumers; format/tidy and diff checks.
Inspect passing codegen, disabled strings, public include graphs, and compile-time
budgets. Do not increase budgets without new measurements and a written reason.

Review for stale noreturn declarations, leaked TLS/ownership after Continue,
reuse of terminal packets, borrowed data escaping, normal logger recursion,
implicit continuation on failure, and accidental production GUI dependencies.
Update docs and runnable examples with actual commands and evidence. Do not claim
all-thread suspension, guaranteed recovery, all-failure delivery, or untested
platform support. Leave the user's unrelated edits untouched. Report changes,
validation, remaining limitations, and any explicit design deviations.
```
