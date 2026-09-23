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
