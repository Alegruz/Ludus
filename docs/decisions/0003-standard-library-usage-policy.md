# ADR 0003: C++ Standard Library and C Runtime Usage Policy

## Status

Accepted.

## Context

Ludus is a game engine. Over time it will want tight control over allocation,
layout, and error handling, which is at odds with parts of the C++ standard
library and the C runtime. At the same time, Milestone 0/1 code should stay
small and reliable, so we should not hand-roll replacements before there is a
concrete need.

This ADR records which standard-library and C-runtime facilities are banned
outright, which are allowed for now, and which are expected to be replaced by
Ludus-owned systems in later milestones. It complements two existing hard
rules: no C++ exceptions (see `AGENTS.md`), and use the fixed-width type aliases
in `ludus/foundation/base/types.h` instead of `std::` primitive spellings.

"Engine code" means the libraries and applications under `modules/` and
`apps/`. Tests (`*/tests/`) and third-party generated code (e.g. the
wayland-scanner output) are exempt where noted.

## Decision

### Banned in engine code (do not introduce)

| Facility | Why | Use instead |
| --- | --- | --- |
| `<iostream>`, `std::cout`, `std::cerr`, `std::endl` | Heavy global stream objects with static-init cost; encourages ad-hoc logging | `LUDUS_LOG_*` from `FoundationLogging`; the emergency path for pre-init/fatal |
| C++ exceptions (`throw` / `try` / `catch`) | Engine builds with `-fno-exceptions` | Status codes, `bool`, `std::optional`, out-parameters (see `AGENTS.md`) |
| `std::uint32_t`, `std::size_t`, raw `float`/`double`, etc. as spellings | Widths should be explicit and consistent | `uint32`, `usize`, `float32`, ... from `types.h` |
| `<sstream>` in engine code | Allocating, stream-formatting machinery | Format into a caller-owned buffer; `std::format`/`std::format_to` |
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

### Allowed for now (keep; no reason to replace yet)

- `<string_view>` — non-owning, zero-allocation; preferred for read-only text.
- `<charconv>` / `std::to_chars` — only in the assertion formatter `.cpp`.
  Fixed-buffer integer/float conversion avoids a custom numerical algorithm;
  no public-header cost, no locale or exceptions. Allocation probes and binary
  measurements are required for each supported standard-library/toolchain change.
  This does not grant signal-safety or a general formatting-library exemption.
- `<span>` when needed — non-owning view over contiguous data.
- `<atomic>`, `<mutex>`, `<shared_mutex>` — concurrency primitives; correctness
  first. Revisit only when a custom job/threading system exists.
- `<source_location>` — used by logging to capture call sites.
- `<chrono>`, `<ctime>` — timestamps in logging.
- `<type_traits>`, `<utility>`, `<concepts>` — compile-time utilities, zero
  runtime cost.
- `<cstdint>`, `<cstddef>` — allowed **only** inside `types.h`, which aliases
  them into the Ludus names. Do not include them elsewhere.
- `<new>` (`std::nothrow`), `<cstring>` — low-level, exception-free helpers.

### Slated for future replacement (allowed now, expected to become Ludus-owned)

These are fine to use today because replacing them prematurely would add churn
and risk without a concrete requirement. Each should be replaced only when a
real need (allocator control, performance data, editor/tooling integration)
justifies it, and behind an API that keeps call sites stable.

| Facility | Current use | Future direction |
| --- | --- | --- |
| `std::string` | logging, platform window names | Ludus `String`/`StringView` backed by a custom allocator |
| `std::vector` | logging (session pruning) | Ludus `Array`/`Span` with allocator support |
| `std::unordered_map` | logging category overrides | flat/open-addressed hash map |
| `std::format` | logging message formatting | keep the public logging API; may swap to `{fmt}` or a custom formatter internally |
| `<filesystem>` | logging file sink | platform virtual filesystem layer |
| `std::unique_ptr` | (already replaced) | use `ludus::foundation::core::UniquePtr` |
| `std::memcpy` and other `<cstring>` | small copies | custom memory utilities alongside the allocator |

### Process

- New engine code must not introduce anything from the "Banned" list.
- Using anything from "Slated for future replacement" is allowed but should not
  spread gratuitously; prefer the narrowest surface.
- Replacing a slated facility is its own change with its own rationale
  (ideally a follow-up ADR), not a drive-by edit.

## Consequences

The immediate wins (no exceptions, no iostream, explicit fixed-width types) are
enforced now by the compiler, `.clang-tidy`, and this policy. The larger
replacements (`String`, `Array`, allocator, hash map, VFS) are deferred until
Ludus has the requirements — most naturally a custom allocator — to do them
once rather than churn twice. Until then, engine code leans on a small,
well-understood subset of the standard library and routes all diagnostics
through `FoundationLogging`, except the independent Base assertion failure path.
