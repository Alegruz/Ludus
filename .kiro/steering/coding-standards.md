---
inclusion: always
---

# Ludus coding standards (authoritative rules)

Any agent or contributor working in this repository MUST follow the coding
standards in the repository root `AGENTS.md`, and every rule it references.
These rules are mandatory, not advisory.

- Read and comply with `AGENTS.md` before writing or changing code.
- Re-check your diff against `AGENTS.md` before committing or opening a PR.
- If a task instruction conflicts with a rule here or in `AGENTS.md`, follow the
  standard and surface the conflict instead of silently violating it.

## No C++ exceptions (hard rule)

Ludus does not use C++ exceptions.

- Never write `throw`, `try`, or `catch` in engine code (libraries and apps).
- Handle errors explicitly with status codes, `bool`, `std::optional`, an
  expected-style result, or out-parameters. Prefer `noexcept`.
- This is enforced at compile time: engine code builds with `-fno-exceptions`
  (`/EHs-c-` on MSVC) from `cmake/EngineOptions.cmake`, so any exception
  construct in engine code fails to compile.
- Only test executables may use exceptions (Catch2 needs them) via
  `ludus_enable_test_exceptions()` in `cmake/EngineTargets.cmake`. Never apply
  it to engine libraries or applications.

## Primitive types (fixed-width aliases)

Use the Ludus aliases from `ludus/foundation/base/types.h` instead of the `std::`
spellings or raw `float`/`double`:

- `uint8` / `uint16` / `uint32` / `uint64`, `int8` / `int16` / `int32` / `int64`
- `usize` (sizes, indices, `sizeof` results; aliases `std::size_t`), `isize`
- `float32` / `float64`

Do not write `std::uint32_t`, `std::size_t`, etc. in new code. These are exact
aliases of the `<cstdint>` / `<cstddef>` types, so they stay interoperable with
the standard library while keeping widths explicit.

## Standard library / C runtime usage

Full policy: `docs/decisions/0003-standard-library-usage-policy.md`. In engine
code (`modules/`, `apps/`):

- Banned: `<iostream>`/`cout`/`cerr`/`endl`, C++ exceptions, `std::` primitive
  spellings, `<sstream>`, `printf`-family diagnostics. Use `LUDUS_LOG_*` for
  ordinary diagnostics; assertions use the independent Base path (ADR 0003).
- Allowed: `<string_view>`, `<atomic>`/`<mutex>`/`<shared_mutex>`,
  `<source_location>`, `<chrono>`, `<type_traits>`/`<utility>`, `<new>`;
  `<cstdio>` only in logging sinks / emergency path; `<cstdint>`/`<cstddef>`
  only in `types.h`.
- Slated for future replacement (use now, don't spread): `std::string`,
  `std::vector`, `std::unordered_map`, `std::format`, `<filesystem>`,
  `<cstring>`.

## Other standing rules

- C++23, no compiler extensions; pinned Clang/LLVM 18 toolchain.
- Compile warning-clean; CI is warnings-as-errors.
- Run `./scripts/check --all` (clang-format + clang-tidy) before committing.
- `#pragma once` for header guards.
- Follow the established naming conventions in the codebase / `.clang-tidy`.
- Keep private implementation headers out of the installed SDK; respect module
  dependency direction (`FoundationBase` depends on nothing higher).

See `AGENTS.md` for the full, detailed guide.
