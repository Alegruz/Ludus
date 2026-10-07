# ADR 0003: C++ Standard Library and C Runtime Usage Policy

## Status

Accepted.

## Context

Ludus needs control over allocation, layout, error handling and portability.
The world demo's dependency on floating-point `std::from_chars` exposed a
library-availability problem on the pinned macOS toolchain: selecting C++23
language mode did not make that operation available. New runtime facilities
must therefore use Ludus-owned implementations unless this policy explicitly
permits a narrowly scoped standard-library or C-runtime operation.

This decision supersedes the earlier permission to introduce STL containers
while waiting for custom allocation. Existing uses remain migration debt;
changing this policy does not require an unrelated repository-wide rewrite.

Production code includes `modules/`, `apps/`, and `examples/`. Test-only code
may use standard-library facilities for fixtures and independent reference
checks, but must exercise the production implementation. Benchmark-only harnesses
may use standard-library/CRT facilities to collect, analyze and report measurements;
this exemption does not apply to the production code being measured or permit
substitute STL implementations of the measured operation. Third-party and
generated code are exempt; their dependencies must remain private and do not
establish permission for production call sites.

## Decision

### Banned in engine code (do not introduce)

| Facility | Why | Use instead |
| --- | --- | --- |
| `<iostream>`, `std::cout`, `std::cerr`, `std::endl` | Heavy global stream objects with static-init cost; encourages ad-hoc logging | `LUDUS_LOG_*` from `FoundationLogging`; the emergency path for pre-init/fatal |
| C++ exceptions (`throw` / `try` / `catch`) | Engine builds with `-fno-exceptions` | Status codes, `bool`, Ludus-owned results, out-parameters (see `AGENTS.md`) |
| `std::uint32_t`, `std::size_t`, raw `float`/`double`, etc. as spellings | Widths should be explicit and consistent | `uint32`, `usize`, `float32`, ... from `types.h` |
| `<sstream>` in engine code | Allocating, stream-formatting machinery | Format into a caller-owned buffer or use the owning Ludus formatter |
| `printf`-family for diagnostics (`std::printf`, `fprintf` to stdout/stderr) | Same reason as iostream; unstructured | `LUDUS_LOG_*` |

`<cstdio>` (`std::fwrite`/`std::fopen`/`std::snprintf`) remains allowed inside
the logging sinks and the emergency path, where a direct, allocation-free,
exception-free write to a file handle or `stderr` is exactly what is wanted.
Do not use it for general engine diagnostics.

Assertions have a sanctioned independent emergency path in FoundationBase.
They must not call normal Logging: the logger can itself be failing or holding
locks. This path uses bounded buffers and private native OS byte output, not
stdio or printf. `<atomic>` is private to the failure runtime; `<cstdlib>` is
allowed privately for `abort`/immediate exit. Neither enters the assertion
header. Ordinary diagnostics still use `LUDUS_LOG_*`. Logging shares Base's final
emergency byte writer; assertion formatting is a separate opt-in layer with a
closed argument set and an implementation-only parser.

### Explicitly permitted operations

- `<string_view>` and `<span>` are existing compatibility interfaces. Maintain
  them where required by an existing API; new view APIs should be Ludus-owned.
- `<charconv>` / `std::to_chars` — only in the assertion formatter `.cpp`.
  Fixed-buffer integer/float conversion avoids a custom numerical algorithm;
  no public-header cost, no locale or exceptions. Allocation probes and binary
  measurements are required for each supported standard-library/toolchain change.
  This does not grant signal-safety or a general formatting-library exemption.
- `<cmath>` and `<limits>` — allowed **only** inside FoundationMath
  implementation files (`modules/foundation/math/src/*.cpp`) and its tests, for
  standard numeric facilities: `std::sqrt`/`std::sin`/`std::cos`/`std::atan2`,
  `std::isfinite`/`std::isnan`, `std::copysign`/`std::nextafter`/`std::expm1`,
  `std::numeric_limits`, etc. (ADR 0010). These are header-only, free-standing
  math functions: they allocate nothing, throw nothing (Math builds with
  `-fno-exceptions` like the rest of the engine), and keep no global state. They
  must **not** appear in a public math header — only `.cpp` files include
  `<cmath>`/`<limits>`, so no heavy facility or substantial template is added to
  the installed API or the build-time budget. This is not a general STL
  exemption and grants nothing beyond standard numeric functions; it does not
  authorise `<random>` distributions (FoundationMath ships its own PCG32), and
  the checked numerical path still forbids fast math and FMA contraction (it is
  compiled with `-ffp-contract=off`).
- `<climits>` and `<limits>` are also allowed **only** in FoundationBase's
  private `src/types.cpp` representation-contract translation unit (ADR 0015).
  Compile-time byte width, integer range, and IEEE binary32/64 verification
  must not add these includes to `types.h` or `core.h`.
- `<atomic>`, `<mutex>`, `<shared_mutex>` — concurrency primitives; correctness
  first. Revisit only when a custom job/threading system exists.
- `<source_location>` — used by logging to capture call sites.
- `<chrono>`, `<ctime>` — only within existing logging/time implementations;
  new engine callers use the owning Ludus API.
- `<type_traits>`, `<utility>`, `<concepts>` — compile-time utilities, zero
  runtime cost.
- `<cstdint>`, `<cstddef>` — allowed **only** inside `types.h`, which aliases
  them into the Ludus names. Do not include them elsewhere.
- `<new>` (`std::nothrow`), `<cstring>` — low-level, exception-free helpers.

### Ludus-owned runtime facilities

New containers, owning strings, smart pointers and algorithms must use Ludus
implementations. This includes `std::vector`, `std::array`, maps/sets, queues,
`std::string`, `std::unique_ptr`/`std::shared_ptr`, standard sorting/searching
algorithms, `<algorithm>`, `<ranges>` and `<numeric>`. Existing Ludus container
and pointer APIs take precedence; implement a missing operation in its owning
module rather than adding an STL-backed alias or wrapper. Specify allocation,
capacity, ownership, failure and iterator/view invalidation where applicable.
Use concepts and type traits to express constraints without adopting runtime
STL machinery.

New numeric text parsing must also be Ludus-owned. Do not substitute
`std::from_chars`, `std::sto*`, locale-sensitive CRT conversion, or a third-party
number-conversion library. Decimal conversion needs a documented grammar,
bounded input, complete-consumption rules, rounding and overflow/underflow
behavior, and output preservation on failure. Tests must cover malformed input,
numeric boundaries and rounding cases on supported native and browser
toolchains. The existing assertion formatter's `std::to_chars` exception above
remains restricted to that implementation; it does not authorize new parsers.

Necessary existing standard-library and CRT uses may be maintained within
their current implementation boundaries, including facilities absent from the
new-code allowlist. Examples include Logging's `std::thread`,
`std::condition_variable` and `std::hash`, Editor's `std::function`, and existing
containers, conversion, formatting and filesystem calls. This compatibility
permission does not authorize adding those facilities to new APIs or subsystems.
Their replacement belongs in focused, validated changes that preserve existing
contracts; existing use is not precedent for new code.

### Process

- Before adding an include or standard-library call, check the exact operation
  against this allowlist and inspect the equivalent Ludus module.
- Allowed headers do not grant blanket permission for everything they expose.
  For example, `<utility>` does not authorize a runtime collection abstraction,
  and `<cstring>` does not authorize locale-sensitive number parsing.
- A missing Ludus facility is implementation work, not an automatic exception.
  Keep replacements focused and cover their meaningful failure/boundary cases.
- A new exception requires an explicit change to this ADR for review: name the
  exact facility and private boundary, explain why it is needed, and supply
  availability and behavior evidence for supported native and browser tools.
- Re-check new includes, `std::` calls and dependency additions before committing.
  Existing uses and successful compilation on one host are not portability proof.

## Consequences

New engine and sample code grows the Ludus runtime instead of extending its STL
dependency. Concepts, traits and explicitly approved low-level primitives remain
available. Existing runtime-library usage is retired through focused migrations.

These restrictions are contributor policy; no new automatic STL gate is added
by this documentation change. Compiler exception enforcement and existing
static-analysis checks remain in place.
