# Native diagnostics and assertion policies

Use this guide for diagnostic-helper launches and build-policy validation.
The [assertion architecture](../architecture/assertions.md) owns the detailed
error and lifetime contracts; [debugging](debugging.md) covers RAD sessions.

## Build flavor and assertion policy

Every preset now supplies an explicit `LUDUS_BUILD_FLAVOR`. Debug uses CMake
Debug; Development and Profile both use RelWithDebInfo; Release uses Release
(or MinSizeRel for a manual build). Direct CMake invocations must supply the
matching flavor. Profile is its own engine flavor even though its optimization
configuration matches Development. Assertion policy never follows `NDEBUG`.

Debug/Development enable `LUDUS_ASSERT` and Check inspection breaks. Profile and
Release compile Assert away and disable Check breaks. Require/Check/Fatal stay
active everywhere. `REQUIRE`/`FATAL` always terminate, including after a debugger
continuation; `CHECK` returns its boolean. Enabled `ASSERT`/`ASSERT_F` is now
**resumable through explicit developer action** in non-CI runs — a debugger
Continue (Debug or Development) or, with no debugger in a non-CI Debug build, the
external helper's Continue-once dialog — otherwise it terminates. This is gated
by the generated `LUDUS_ASSERT_DIALOGS_AVAILABLE` (1 only for non-CI Debug) plus
a runtime CI veto; the assertion-policy version is 2. See
[interactive assertion contract](../architecture/assertions.md#51-interactive-development-resumable-assert) and
[ADR 0006](../decisions/0006-resumable-development-assertions.md).

The installed `assert_config.hpp` and SDK manifest carry the built variant's
policy. A Release consumer of a Development SDK uses Development's assertion
policy. Do not override the generated macros or mix headers/libraries from
different variants. Use separate prefixes. Installation refuses an incompatible
or legacy unversioned Ludus prefix before overwriting files; move an old generated
`out/install/<preset>` aside, then rerun `scripts/install-sdk`. Multi-config SDK
generation is explicitly unsupported until per-configuration packages exist.

### Diagnostic helper and report delivery

The external diagnostic helper is a Python development tool,
`tools/diagnostics/ludus_diagnostic_helper.py` (installed to the SDK `bin`
directory as `ludus_diagnostic_helper`). It launches an engine binary with the
diagnostic channels wired up so assertion/`FATAL`/`CHECK` reports are captured
independently of normal Logging. Run an engine binary under it with:

```bash
# Convenience launcher (resolves the installed or source-tree helper):
scripts/run out/build/linux-clang-debug/apps/smoke/ludus_smoke
# Or invoke the helper directly:
python3 tools/diagnostics/ludus_diagnostic_helper.py -- <engine-binary> [args...]
# or, from an installed SDK:
ludus_diagnostic_helper -- <engine-binary> [args...]
```

A binary launched **directly** (without the helper) also works: it configures
diagnostics at startup and runs report-only when no helper/channel is present.
When launched under the helper in a local, non-CI **Debug** build with a usable
terminal or live display, a
failed `ASSERT` presents a **Continue once / Terminate** prompt; *Continue once*
resumes past that assertion (the invariant is now known-broken), a second failure
is reported again, and *Terminate* exits. CI and headless runs stay report-only
and never wait for input.

The engine calls `ludus::diagnostics::InitializeDiagnosticSession()` (from
`ludus/diagnostics/session.hpp`, in the `Ludus::DiagnosticsIntegration` target,
which depends on FoundationBase but is not part of it) once at the top of `main`,
before workers or the logger. It reads these descriptors/policy from the
environment:

- `LUDUS_DIAGNOSTIC_REPORT_FD` — connected `AF_UNIX`/`SOCK_DGRAM` report socket.
- `LUDUS_DIAGNOSTIC_CONTROL_FD` — connected `AF_UNIX`/`SOCK_SEQPACKET` control
  socket (versioned Hello/HelloAck handshake).
- `LUDUS_DIAGNOSTIC_INTERACTIVE=0` — force report-only (local headless Debug).

The helper sets the first two for its child; a directly launched binary with no
helper simply runs report-only with no transport. Interactive presentation is
eligible only in a local, non-CI Debug build with a live terminal/display; CI is
auto-detected and forced report-only, and CI workflows additionally set
`LUDUS_DIAGNOSTIC_INTERACTIVE=0` explicitly. This layer changes no assertion's
fatal action.

To run Release policy tests without changing the production preset:

```bash
out/host-tools/venv/bin/cmake --preset linux-clang-release -B out/build/linux-clang-release-assert-tests -DLUDUS_BUILD_TESTS=ON -DLUDUS_WARNINGS_AS_ERRORS=ON
out/host-tools/venv/bin/cmake --build out/build/linux-clang-release-assert-tests
out/host-tools/venv/bin/ctest --test-dir out/build/linux-clang-release-assert-tests --output-on-failure
```

