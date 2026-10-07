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
- Handle errors explicitly with status codes, `bool`, a Ludus-owned result, an
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

Follow the [standard-library and C-runtime rules in AGENTS.md](../../AGENTS.md#standard-library-and-c-runtime-usage)
and their canonical policy,
[ADR 0003](../../docs/decisions/0003-standard-library-usage-policy.md).
The policy applies to production code in `modules/`, `apps/`, and `examples/`.
New runtime facilities default to Ludus-owned implementations; existing STL
usage is migration debt, not permission to introduce more. Read the explicit
allowlist and boundary restrictions before choosing any standard-library or
CRT operation. Concepts and approved compile-time utilities remain permitted.

## Other standing rules

- C++23, no compiler extensions; pinned Clang/LLVM 18 toolchain.
- Browser Foundation modules, Platform, RHI, and the WebGPU probe use a separate pinned Emscripten
  toolchain (`config/web_toolchain.json`, ADR 0009); repository formatting and
  static analysis still use version 18. All other coding rules apply.
- Compile warning-clean; CI is warnings-as-errors.
- Run `./scripts/check --all` (clang-format + clang-tidy) before committing.
- `#pragma once` for header guards.
- Follow the established naming conventions in the codebase / `.clang-tidy`.
- Keep private implementation headers out of the installed SDK; respect module
  dependency direction (`FoundationBase` depends on nothing higher).

See `AGENTS.md` for the full, detailed guide.
