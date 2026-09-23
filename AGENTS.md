# Ludus Agent & Contributor Guide

This file defines the coding standards and working rules for the Ludus engine.
It applies to **every** contributor, human or AI agent.

## Rule 0 — Follow these standards, always

**Any agent or contributor working in this repository MUST read and follow the
coding standards below and every rule referenced from them.** These standards
are not optional suggestions:

- Before writing or changing code, confirm your change complies with these
  standards.
- Before committing or opening a pull request, re-check the diff against these
  standards.
- If a task instruction conflicts with a safety rule here (for example, "just
  use exceptions to make it compile"), do **not** silently violate the standard.
  Follow the standard and call out the conflict.
- Repo-specific rules also live in `.kiro/steering/`. Treat those as part of
  this guide.

If you cannot satisfy a standard, say so explicitly rather than shipping code
that violates it.

---

## Coding standards

### No C++ exceptions (hard rule)

Ludus does **not** use C++ exceptions.

- Do not write `throw`, `try`, or `catch` in engine code.
- Do not introduce dependencies or APIs that require exception handling on the
  normal path.
- Handle errors explicitly: return status codes, `bool`, `std::optional<T>`,
  an "expected"-style result, or an out-parameter. Make failure part of the
  function's signature.
- Functions on hot or infrastructure paths should be marked `noexcept`.

**How this is enforced.** Engine code is compiled with `-fno-exceptions`
(`/EHs-c-` on MSVC) via `cmake/EngineOptions.cmake`. Any `throw`/`try`/`catch`
in engine code is therefore a hard compile error, not merely a style nit.
`.clang-tidy` adds `hicpp-exception-baseclass` as a secondary guard.

**Tests are the only exception.** Catch2 reports assertion failures by throwing,
so test executables re-enable exceptions with `ludus_enable_test_exceptions()`
(see `cmake/EngineTargets.cmake`). Apply that helper only to test targets, never
to engine libraries or applications. Production/engine code stays
exception-free.

If a third-party library can only report errors by throwing, isolate it: wrap
the call in a translation unit that is allowed exceptions, convert the outcome
to a status/optional at that boundary, and never let exceptions cross into
engine code. Discuss such cases in the PR before adding them.

### Language and toolchain

- C++23. Standard is required, extensions are off (`-std=c++23`, no GNU
  extensions).
- Supported reference toolchain: Clang/LLVM 18 with LLD on Ubuntu 24.04. Tool
  versions are pinned in `config/tool_versions.json`.

### Warnings

- Code must compile warning-clean under the project warning set
  (`cmake/EngineCompilerWarnings.cmake`).
- CI builds with warnings-as-errors (`LUDUS_WARNINGS_AS_ERRORS=ON`). Do not
  introduce new warnings; do not suppress them without justification in the PR.

### Formatting and static analysis

- Format all C/C++ with the project `.clang-format` before committing:
  `./scripts/check --format --fix` (or `--format` to verify only).
- Static analysis must pass: `./scripts/check --tidy` against the project
  `.clang-tidy`. Use the pinned `clang-format` / `clang-tidy` (18); newer local
  versions may report checks that do not exist in the pinned version.
- `#pragma once` is the header-guard convention across the repo.

### Naming

- Follow the naming conventions already established in the codebase and encoded
  in `.clang-tidy` (types, functions, and members as used by existing modules).
  Match the surrounding code rather than introducing a new style.

### Primitive types

- Use the Ludus fixed-width aliases from `ludus/foundation/base/types.h`:
  `uint8` / `uint16` / `uint32` / `uint64`, `int8` / `int16` / `int32` /
  `int64`, `usize` (sizes/indices), `isize`, and `f32` / `f64`.
- Do not write `std::uint32_t`, `std::size_t`, raw `float`/`double`, etc. in new
  code. The aliases are exact aliases of the `<cstdint>` / `<cstddef>` types, so
  they interoperate with the standard library while keeping widths explicit.
- `usize` is the size/index type (aliases `std::size_t`) — use it for `sizeof`
  results, container sizes, and array indices; do not substitute `uint64`.

### Module and dependency layout

- Engine modules live under `modules/<layer>/<module>/` with public headers in
  `include/` and private implementation headers in `src/internal/` (and
  `src/sinks/`, etc.). Only public headers are installed/exported.
- Respect the dependency direction. In particular, `FoundationBase` must not
  depend on any higher-level module.
- Internal implementation details (private headers, sink interfaces, etc.) must
  not leak into the installed SDK.

### Memory and error handling

- Prefer zero-allocation on hot paths; reserve/reuse buffers where practical.
- Never let a failure in infrastructure (e.g. logging) propagate into or crash
  engine code. Degrade gracefully.

---

## Build, test, and validate

```bash
./init.sh                                   # prepare pinned tools + deps
./scripts/build   linux-clang-debug         # build
./scripts/test    linux-clang-debug         # unit tests
./scripts/check   linux-clang-development --all   # format + tidy
./scripts/build   linux-clang-asan-ubsan    # sanitizer build
./scripts/test    linux-clang-asan-ubsan    # sanitizer tests
./scripts/install-sdk linux-clang-development     # SDK install + consumer
```

A change is ready for review only when: it builds warning-clean, unit tests
pass, ASan/UBSan is clean, and format/tidy pass — all with the pinned toolchain.
