# ADR 0004: Type-Erase std::format at the Logging ABI Boundary

## Status

Accepted.

## Context

Build-time profiling (`./scripts/profile-build`, see
`docs/development/build-profiling.md`) showed that `log.hpp` was by far the most
expensive header in the engine — ~19 s of aggregate parse time across the
translation units that log, and the dominant entry in every "expensive
templates / functions" list. The cause was structural: the public `Log(...)`
function template called `std::vformat(...)` **in the header**, so the entire
`<format>` instantiation machinery (`std::vformat_to`, `_Formatting_scanner`,
the per-type `__formatter_*`, both `char` and `wchar_t` sinks) was re-generated
in every logging translation unit.

## Decision

Erase the format arguments to `std::format_args` at the call site and perform
the actual formatting in a single non-template function compiled once in
`logger.cpp`.

```cpp
// log.hpp — trivial pack expansion only; no <format> machinery instantiated
template <typename... Args>
void Log(LogLevel lvl, LogCategory cat, const std::source_location& loc,
         std::format_string<Args...> fmt, Args&&... args)
{
    if constexpr (sizeof...(Args) == 0)
        detail::DispatchMessage(lvl, cat, loc, fmt.get());          // no formatting
    else
        detail::VLog(lvl, cat, loc, fmt.get(), std::make_format_args(args...));
}

// logger.cpp — std::vformat instantiated exactly once for the whole engine
void detail::VLog(LogLevel, LogCategory, const std::source_location&,
                  std::string_view fmt, std::format_args args);
```

Call sites keep compile-time format-string checking (`std::format_string`) and
still pay only the cheap `std::make_format_args` pack expansion. `std::vformat`
and its template explosion live in one place.

## Consequences

Measured on the `linux-clang-development` preset (Clang 18, `-ftime-trace`),
clean build, before vs after:

| Metric | Before | After | Change |
| --- | --- | --- | --- |
| Frontend parsing (summed) | 52.5 s | 22.5 s | −57% |
| Backend codegen (summed) | 21.9 s | 9.0 s | −59% |
| Slowest single TU | 5.8 s | 2.7 s | −54% |
| `log.hpp` aggregate parse | ~19.0 s | ~9.0 s | −53% |

The `std::__format::*` / `std::vformat_to` template instantiations that
dominated the baseline no longer appear in the per-TU profile; the `wchar_t`
format paths are no longer instantiated per TU either.

The residual `log.hpp` cost is `<format>` being **parsed** (still included for
`std::format_string` / `std::make_format_args`), plus `<chrono>` pulled in
transitively. Those are the next levers (precompiled header for the stable heavy
STL, or trimming the header further); they are tracked separately and must be
justified with a `profile-build` measurement.

## Generalization

This establishes a project rule (see `AGENTS.md`): **do not instantiate heavy
standard-library templates in public headers.** Expensive facilities are
type-erased or PIMPL'd behind a `.cpp` boundary; templated public APIs erase to
a non-template implementation as early as possible.



## Amendment (logging redesign, phases 0–4)

The original decision type-erased `std::format` to `std::format_args` so that the
`std::vformat` **instantiation** happened once instead of per translation unit.
That removed replicated *execution machinery*, but it did **not** remove the cost
of *parsing* `<format>` (and its transitive `<chrono>`), which every logging TU
still paid because `log.hpp` included `<format>`. Erasure of execution and
elimination of parse cost are different things; this ADR previously conflated
them.

The logging redesign supersedes the `std::format` boundary with a narrow,
bounded, no-heap typed formatter (`internal/format_engine.{hpp,cpp}`), selected
after measuring both candidates on the pinned toolchain
(`.kiro/specs/logging-redesign/decision-log.md`):

- **Build:** the common `log.hpp` no longer includes `<format>`/`<filesystem>`;
  measured preprocessed expansion fell from ~79,790 to ~20,232 lines, and the
  opt-in `log_format.hpp` from ~65,599 to ~21,213 lines (clang-18, this host).
- **Contract:** unlike `std::vformat`, the narrow formatter never allocates and
  never terminates on a bad/oversize format under `-fno-exceptions`; it returns
  `{BytesWritten, Truncated, FormatError}`. This is a hard acceptance gate the
  `std::format` path could not meet, and was the deciding factor — not build time
  alone.
- **Cost accepted:** the narrow packer adds ~40 bytes of `.text` per call site
  versus `make_format_args`; this is bounded and tracked, and does not violate
  any hard gate.

`std::format` is therefore no longer used anywhere in the logging headers or
engine translation units. The temporary `log_compat_format.hpp` adapter
contemplated by the spec was not needed: every existing call site already fits
the narrow grammar. The rule this ADR established — *do not instantiate heavy
standard-library templates in public headers* — still holds and is now enforced
by header parse budgets (ADR 0005) plus the split-header structure.
