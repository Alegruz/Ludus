# Assertions — Interactive Development (resumable `ASSERT`) — Design

**Status:** proposed, not implemented. Traces to
[`requirements.md`](requirements.md) (R-IDs),
[ADR 0006](../../../docs/decisions/0006-resumable-development-assertions.md), and
[`docs/architecture/assertions.md` §5.1 / §12](../../../docs/architecture/assertions.md).
Where this design and the checked-out runtime diverge, the runtime is the
baseline and the delta is called out. Nothing here is implemented; this document
describes the intended runtime shape so a later prompt can build it.

`[OPEN]` markers are genuine decisions deferred to a later prompt; `[CD]` markers
are reversible conservative defaults.

---

## 0. One-paragraph architecture

Keep the entire success path and the entire `REQUIRE`/`FATAL`/`CHECK` contract
exactly as shipped. Introduce resumability for enabled `ASSERT`/`ASSERT_F` as a
new branch **inside** the existing `FinishFatal*` runtime, taken only when the
build is eligible (`LUDUS_ASSERT_RESUMABLE == 1`, Debug flavor) and, at failure
time, the run is non-CI and an explicit Continue-once action is obtained. The
report is built and published first (unchanged ordering); only then does the
runtime consult a new private, detached Base backend — a CI-environment query
and, when no debugger is attached, an interactive Continue-once / Terminate
prompt — reusing the same OS-primitive boundary as `QueryDebugger` /
`BreakForDebugger`. A "Continue once" for `ASSERT` releases the reporting slot
and returns to the caller like a completed `CHECK`; anything else terminates.
`assert.hpp`, the success path, and all public headers are unchanged.

---

## 1. Header and dependency graph (R5, R6, R12)

No public-header change. `assert.hpp` still includes only `assert_config.hpp`,
`compiler.hpp`, `types.h`. The `ASSERT`/`ASSERT_F` macros still expand to
`BeginFatal(FailureKind::Assert, site)` then `FinishFatal*`; the new behavior is
selected inside `assert.cpp`, gated on the generated `LUDUS_ASSERT_RESUMABLE`.

New private backend surface, all under `modules/foundation/base/src/`:

```text
src/internal/diagnostic_platform.hpp   (existing) + two new declarations:
    enum class ResumeDecision : uint8 { ContinueOnce, Terminate };
    bool DetectContinuousIntegration() noexcept;      // failure-path only
    ResumeDecision PromptAssertDecision(               // no debugger attached
        const char* report, usize size) noexcept;      // degrades to Terminate

src/diagnostics_linux.cpp              (existing) + Linux implementations:
    - DetectContinuousIntegration(): failure-path env inspection (D2)
    - PromptAssertDecision(): interactive Continue-once / Terminate (D1)
```

These are private (`src/internal`), never installed (R6), never included by any
public header (R5/R12), and compiled only into Base. Unsupported platforms leave
these unimplemented so their configuration fails (consistent with the existing
"unsupported backend selection must fail configuration" rule).

## 2. Generated policy surface (R20–R23)

Add one numeric value to the generated `assert_config.hpp` and its CMake source:

| Symbol | Debug | Development | Profile | Release / MinSizeRel |
| --- | :-: | :-: | :-: | :-: |
| `LUDUS_ENABLE_ASSERTS` | 1 | 1 | 0 | 0 |
| `LUDUS_BREAK_ON_CHECK` | 1 | 1 | 0 | 0 |
| `LUDUS_ASSERT_RESUMABLE` (**new**) | **1** | 0 | 0 | 0 |
| `LUDUS_ASSERT_POLICY_VERSION` | 2 (**bumped**) | 2 | 2 | 2 |

In `cmake/EngineBuildFlavor.cmake`, derive `LUDUS_ASSERT_RESUMABLE` from the
flavor id (1 iff `LUDUS_BUILD_FLAVOR_ID == 1`), add it to the forbidden-override
loop, and bump `LUDUS_ASSERT_POLICY_VERSION` to `2`. In
`cmake/assert_config.hpp.in`, add the `#error`-guarded `#define`. In
`cmake/LudusSdkManifest.json.in`, add an `enable_resumable` field. The SDK
variant string already encodes flavor/config, so it changes automatically; the
new manifest field and version bump make the policy change explicit and
consumer-observable (R22). Multi-config generators remain rejected (R23).

This value expresses **build-time eligibility only**. It never by itself causes
a resume; the runtime conditions in §3 are still required.

## 3. Runtime control flow (R30–R47)

The change is localized to the fatal finish path in `assert.cpp`. Today
`FinishFatalImpl` does: build+publish complete report → primary emergency write
→ `InspectIfAttached()` → `TerminateForAssertion()`. The new shape:

```text
FinishFatalImpl(kind, message/rendered):
    build + publish complete report            # unchanged (R40)
    primary emergency write                    # unchanged (R40)

    resumable = (LUDUS_ASSERT_RESUMABLE != 0)
                && kind == Assert               # only ASSERT/ASSERT_F (R10)
                && !recursive/contended          # already handled in BeginFatal (R43)

    if resumable && !DetectContinuousIntegration():   # runtime CI veto (R31)
        if QueryDebugger() == Attached:
            BreakForDebugger()                  # resume point: continue => return (R32)
            decision = ContinueOnce             # a returned break means "continue"
        else:
            decision = PromptAssertDecision(report, size)  # (R33/R34/R35)
        if decision == ContinueOnce:
            release slot + clear TLS (like CHECK)          # (R41/R42)
            return                              # ASSERT returns to caller (R13)

    # every other path: REQUIRE/FATAL, Development, CI, headless, Terminate,
    # recursion/contention
    InspectIfAttached()                          # unchanged for terminal kinds
    TerminateForAssertion()                      # unchanged (R10/R24)
```

Notes tying to requirements:

- **Kind gating (R10/R11):** only `FailureKind::Assert` can enter the resumable
  branch. `Require`/`Fatal` fall straight to the unchanged terminal tail.
  `CHECK` never enters `FinishFatal*` at all.
- **Ordering (R40):** the resumable branch is inserted at exactly the existing
  break-then-terminate point, after the complete report is published and the
  primary write attempted, so the committed evidence is identical whether or not
  a prompt is later shown or faults.
- **Debugger continue (R32):** `BreakForDebugger()` "deliberately may return on
  debugger resume." For `ASSERT` in an eligible non-CI run, a returned break is
  interpreted as Continue-once. For `REQUIRE`/`FATAL`, the break is only reached
  in the terminal tail, so a returned break still terminates.
- **Detached prompt (R33/R34/R35):** `PromptAssertDecision` presents exactly two
  choices, returns `Terminate` on any non-interactive/EOF/absent-helper/malformed
  condition, never auto-continues, and has no timeout-continue. It is passed the
  already-built report bytes so it can show context without re-formatting.
- **Owned lifetime / slot release (R41/R42):** a resumed `ASSERT` must use
  `CHECK`-style stack-owned reporting and the `CHECK` slot-release/TLS-clear
  sequence, and must never write into `gFatalPacket`. This is the main runtime
  delta beyond the branch: today `FinishFatalImpl` publishes into the terminal
  packet, so the implementation must construct the `ASSERT` report the way
  `FinishCheckImpl` does when the build is resumable. `[CD]` model a resumable
  `ASSERT` internally as "a fatal-kind report that may return," reusing the
  `CHECK` ownership/slot machinery for its report and release.
- **Recursion/contention (R43):** unchanged. `BeginFatal` already terminates
  immediately on a recursive or contended failure before any report; those never
  reach the resumable branch.
- **Budget (R44):** a resumed `ASSERT` decrements/consults a presentation budget
  like `CHECK`. `[OPEN]` D3: shared `CHECK` slot budget vs a dedicated counter.
- **Transport/helper loss (R45):** `TryWriteEmergencyBytes` already records
  delivery status without blocking; `PromptAssertDecision` returns `Terminate`
  when its channel is gone. Neither blocks termination.
- **Success path (R46):** the new queries live only in `FinishFatalImpl`, which
  is on the cold failure path. No success-path symbol is added.

## 4. Build/execution matrix (R52)

Outcome of an **enabled `ASSERT` failure** (the resumable kind). `REQUIRE`/`FATAL`
always terminate; `CHECK` always returns `false`.

| Flavor | Under CI? | Debugger | Interactive I/O | Outcome |
| --- | :-: | :-: | :-: | --- |
| Debug (`RESUMABLE=1`) | No | Attached | n/a | Break; **continue ⇒ return to caller**, else terminate on the continue that reaches the tail |
| Debug | No | Detached | Available | **Continue-once / Terminate prompt**; Continue ⇒ return, Terminate ⇒ terminate |
| Debug | No | Detached | Absent (headless/closed/EOF/no helper) | **Terminate** (no default-continue) |
| Debug | **Yes (CI veto)** | any | any | **Terminate** (no prompt, no resume) |
| Debug | any | any | any, **recursive/contended** | **Immediate termination** (never resumable) |
| Development (`RESUMABLE=0`) | any | Attached | any | Report + break, then **terminate** (report-only) |
| Development | any | Detached | any | Report, then **terminate** (report-only; no prompt) |
| Profile / Release / MinSizeRel | — | — | — | `ASSERT` **not compiled**; unaffected |

Direct-executable startup (R36): each row is evaluated from the live process
environment/descriptors at failure time, so a directly launched Debug binary in
an interactive non-CI shell prompts, while the same binary piped/headless or
under CI terminates — no launcher handshake is assumed.

## 5. Backend contract details

- **`DetectContinuousIntegration()`** — cheap, failure-path-only, reads the
  process environment. `[OPEN]` D2 fixes the exact signal set; `[CD]` treat a
  non-empty `CI` environment variable as "CI" for the first cut. Returns a plain
  `bool`; on any doubt it should bias toward `true` (terminate) rather than
  toward prompting a runner, but must not misfire for ordinary developer shells.
- **`PromptAssertDecision(report, size)`** — presents Continue-once / Terminate
  when no debugger is attached. `[OPEN]` D1 fixes the channel (`/dev/tty` vs
  stdin/stdout vs out-of-process helper). Contract regardless of channel: no
  allocation on the approved path (R47), no blocking that can hang termination
  (R45), returns `Terminate` on EOF/closed/non-interactive/malformed/absent
  helper (R34/R35), bounded re-prompts then `Terminate`, and no auto-continue
  timeout.
- Both are `noexcept`, add no public-header surface, and are only referenced from
  the cold `FinishFatalImpl` path.

### 5.1 Startup / report-delivery layer (implemented — T1.5)

Ahead of the decision itself, the channels and report delivery are built by a
separate milestone (see `docs/architecture/assertions.md` §5.2). It provides,
without changing any assertion action:

- A versioned control endpoint in Base `diagnostic_output.hpp/.cpp`
  (`ConfigureControlEndpoint`, `SOCK_SEQPACKET`, `Hello`/`HelloAck`). Its wire
  format is an **explicit little-endian byte encoding** (16-byte header +
  payload) via `EncodeControlHeader`/`DecodeControlHeader`, not a padded C++
  struct. `DecisionRequest`/`DecisionReply` kinds are reserved in the layout but
  not yet sent — they are the wire form the T2/T3 decision will use.
- A startup integration target **above** Base, `Ludus::DiagnosticsIntegration`
  (`tools/diagnostics/`, header `ludus/diagnostics/session.hpp`,
  `InitializeDiagnosticSession`) that reads inherited descriptors, configures the
  report (reused `SOCK_DGRAM`) and control transports, resolves interactive vs
  report-only, validates a live terminal/display, and reports failed setup
  visibly. It never launches a helper or UI. Base does not depend on it.
- The external Python helper `tools/diagnostics/ludus_diagnostic_helper.py`
  (owns collector ends, launches the engine, drains reports to its own output,
  answers the byte-encoded handshake, cleans up on child exit).
- Shared CI detection: Base `IsContinuousIntegration()` (over the private
  `DetectContinuousIntegration`) is used by startup to force report-only and is
  the same query the failure-path CI veto (R31) will reuse. The terminal/display
  probes live in the integration layer (startup-only).

T2 therefore adds only the prompt and the decision-frame exchange over this
existing channel; T3 wires the decision into `FinishFatalImpl`.

## 6. What explicitly does **not** change

- `assert.hpp` / `assert_format.hpp` contents, line counts, include graph (R7,
  R12); the four-name taxonomy and macro expansions.
- `BeginFatal`/`BeginCheck` recursion, ownership, and budget logic (R43); the
  single-incident terminal `gFatalPacket` (R41).
- `CHECK`/`CHECK_F` evaluation, budget, and return contract (R11).
- The datagram transport, emergency byte writer, formatter, logging boundary,
  and single-config/separate-prefix SDK model (beyond one policy value + one
  manifest field).
- Development/Profile/Release behavior for any kind (R24/R25).
