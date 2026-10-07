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
- Browser Foundation modules, Platform, RHI, and the isolated feasibility probe use the Emscripten SDK and WebGPU
  port pinned in `config/web_toolchain.json` (ADR 0009). Its SDK-provided LLVM
  is a separate web toolchain; native builds and formatting/static analysis
  continue to use the reference tools above. Foundation modules, the canvas
  Platform backend, and the RHI lifecycle/frame clear path are available as
  browser engine targets; the W5 probe and integrated W6 smoke app privately
  own their WGSL triangle pipelines. Web presets also build apps/smoke.

### Warnings

- Code must compile warning-clean under the project warning set
  (`cmake/EngineCompilerWarnings.cmake`).
- CI builds with warnings-as-errors (`LUDUS_WARNINGS_AS_ERRORS=ON`). Do not
  introduce new warnings; do not suppress them without justification in the PR.

### Formatting and static analysis

- Format all C/C++ with the project `.clang-format` before committing:
  `./scripts/check --format --fix` (or `--format` to verify only).
- Designated initializers may be compact (`value = { .A = 0, .B = 1 };`).
  Multiline initializers put the opening brace on its own line, with one member
  per line and a trailing comma. Never use `value = {` followed by multiline
  members. Use `scripts/check`, which extends clang-format 18
  for initializer braces; direct clang-format 18 does not implement this rule.
- Local `init.sh` installs the staged auto-formatting hook; existing checkouts
  can enable it with `./scripts/install-hooks`.
- Static analysis must pass: `./scripts/check --tidy` against the project
  `.clang-tidy`. Use the pinned `clang-format` / `clang-tidy` (18); newer local
  versions may report checks that do not exist in the pinned version.
- `#pragma once` is the header-guard convention across the repo.

### Naming

- Follow the naming conventions already established in the codebase and encoded
  in `.clang-tidy` (types, functions, and members as used by existing modules).
  Match the surrounding code rather than introducing a new style.

### Public SDK documentation

- Treat documentation as part of every new feature. Include appropriate Doxygen
  comments for its new or changed public API in the same change, following the
  requirements below.
- When a new feature needs setup instructions, usage examples, or an explanation
  of its workflow or limitations, update its canonical Markdown page under
  `docs/` in the same change. The wiki renders that file; do not write a second
  version for publication.
- Document new or changed public classes, structs, functions, methods, aliases,
  enum values, constants and macros beside their declarations using Doxygen
  `///` comments (or `/** ... */`). Plain `//` comments are not API descriptions.
- State purpose and the meaningful contract: parameters and units, return/status
  meanings, failure/output preservation, ownership/lifetime, thread affinity,
  and platform/build limitations where applicable. Use `@param`, `@return`,
  `@pre`, `@note`, and `@warning` when useful. Do not invent guarantees or repeat
  the signature as a substitute for behavior. No `@throws`: engine errors are
  explicit and exception-free.
- Preserve reference attribution. Detailed architecture prose belongs in its
  existing owner; link to it rather than duplicating it in every declaration.
- Public header inventory is checked against literal CMake public file sets.
  Private code, third-party headers and `detail`/`internal` helpers must not enter
  the public reference. Generated SDK policy headers are documented from templates.
- Build MkDocs, then run `python scripts/build-api --bootstrap` (or provide the
  pinned executable with `--doxygen`) and `python scripts/check-wiki` when changing
  public API documentation or its tooling. See `docs/development/api-reference.md`.
- CI rejects undocumented symbols outside `docs/api-undocumented.json`, including
  removal of existing descriptions. This baseline records legacy debt; do not add
  new gaps or regenerate it to bypass checks. Remove resolved entries in the change that documents them.
  Generated HTML/XML/report files stay in ignored `out/`, never source control.

### One documentation source, online and offline

- `docs/` is the canonical source of authored documentation. `docs/wiki/`
  contains task guides and navigation, not separately maintained copies of
  engineering documents. Publish the same Markdown content and local assets
  through MkDocs. Presentation and navigation may differ; contracts, examples,
  status and attribution must not.
- Edit the existing owner before adding a page. A guide and a design review may
  serve different reader needs, but link to the owner of detailed contracts
  instead of restating them. Never require contributors to synchronize prose
  between a local document and a wiki page.
- Keep module, sample and tool READMEs as short links to their guide under `docs/`,
  so those instructions enter the wiki and offline bundle. Label historical
  milestone scope and link current usage/evidence before retaining older plans.
- Use relative Markdown links for destinations inside `docs/` and local files
  for essential images. Keep examples and essential instructions readable in a
  plain text/Markdown reader without a website, plugin, network request or
  include directive. External reference citations may remain online.
- API comments beside public declarations are the canonical API contract;
  Doxygen/MkDocs output is generated from them, never edited independently.
- Keep the local index current with `python scripts/check-docs --fix-index`.
  Run `python scripts/check-docs`, a strict MkDocs build and
  `python scripts/check-wiki --documentation-only` for documentation changes.
  Publication additionally checks the combined browser editor payload without
  that flag. The checks require every
  authored page to appear in the site and reject stale source/output manifests.
  See `docs/development/wiki.md` for API generation and offline packaging.
- Ship Markdown sources and the checked HTML site together for offline use.
  Package an existing build with `python scripts/package-docs`; reading the
  bundle must not download dependencies or regenerate documentation.

### Reference comments and attribution

- Whenever a paper, article, book chapter, or technical document materially
  informs a design, algorithm, or implementation, acknowledge and thank its
  authors with a concise reference comment near the affected code. For a design
  spanning a file or subsystem, place the comment at the relevant file or section
  boundary and link to the detailed design review when available.
- Identify the author or organization, exact title, and publication/book with
  chapter, section, or page numbers as applicable. Include a stable URL or DOI
  when available. Explain which idea was adopted or adapted and any significant
  departure; a bibliography in a design document supplements these code comments.
- Credit sources actually consulted and relevant to the implementation. Verify
  citation details and distinguish design inspiration from copied/adapted code.
  Preserve existing attribution and license notices when editing or refactoring.

### Primitive types

- Use the Ludus fixed-width aliases from `ludus/foundation/base/types.h` (also
  surfaced, with the rest of the foundational vocabulary, by
  `ludus/foundation/base/core.h`):
  `uint8` / `uint16` / `uint32` / `uint64`, `int8` / `int16` / `int32` /
  `int64`, `usize` (sizes/indices), `isize`, and `float32` / `float64`.
- Do not write `std::uint32_t`, `std::size_t`, raw `float`/`double`, etc. in new
  code. The aliases are exact aliases of the `<cstdint>` / `<cstddef>` types, so
  they interoperate with the standard library while keeping widths explicit.
- Byte widths and IEEE binary32/binary64 representation are checked by
  FoundationBase (ADR 0015). `usize`/`isize` remain native sizes/differences;
  serialized fields must specify a fixed width and byte order.
- Opt in to `checked_integer.hpp` for `TryIntegerCast` and checked
  add/subtract/multiply at numeric boundaries; `byte_order.hpp` provides bounded
  unsigned little/big endian codecs. These preserve outputs on failure and are
  never included by `types.h`/`core.h`. See `docs/architecture/primitive-types.md`.
- `usize` is the size/index type (aliases `std::size_t`) — use it for `sizeof`
  results, container sizes, and array indices; do not substitute `uint64`.

### Standard library and C runtime usage

The full policy — what is banned, allowed, and slated for future replacement —
is `docs/decisions/0003-standard-library-usage-policy.md`. Summary for engine
code (`modules/` and `apps/`):

- **Banned:** `<iostream>` / `std::cout` / `std::cerr` / `std::endl`, C++
  exceptions, `std::` primitive-type spellings, `<sstream>`, and `printf`-family
  for diagnostics. Route ordinary diagnostics through `LUDUS_LOG_*`; assertion
  failures use the independent FoundationBase emergency path (ADR 0003).
- **Allowed:** `<string_view>`, `<atomic>` / `<mutex>` / `<shared_mutex>`,
  `<source_location>`, `<chrono>`, `<type_traits>` / `<utility>`, `<new>`; and
  `<cstdio>` only inside logging sinks / the emergency path. `<cstdint>` /
  `<cstddef>` only inside `types.h`.
- **Slated for future replacement (fine to use now, do not spread):**
  `std::string`, `std::vector`, `std::unordered_map`, `std::format`,
  `<filesystem>`, `<cstring>`. These become Ludus-owned types once a custom
  allocator exists; replacing one is its own change, not a drive-by edit.

### Build-time hygiene (heavy headers)

- **Do not instantiate heavy standard-library templates in public headers, and
  do not `#include` heavy headers (`<format>`, `<chrono>`, `<filesystem>`,
  `<regex>`, `<iostream>`) from a public header.** Type-erase or PIMPL the
  expensive facility behind a `.cpp` boundary; templated public APIs should erase
  to a non-template implementation as early as possible (see ADR 0004: the
  logging `Log()` erases to `std::format_args`).
- Build time is budgeted, not vibes. `config/build_budget.json` sets a per-header
  average parse-time budget; the `Build-time budget` CI job (and
  `./scripts/check-build-budget`) fails if a project header exceeds it. If a build
  gets slow, run `./scripts/profile-build` and fix the offending include; raise a
  budget only with a measurement and a note in the PR. See
  `docs/development/build-profiling.md` and ADR 0005.
- `./scripts/check --include-cleaner` gives an advisory include-hygiene report;
  it is not a gate.

### Foundational headers and the include boundary

Ludus has a two-tier include model (ADR 0007;
`docs/architecture/foundational-headers.md`). Learn these three lists — they are
enforced by the `ludus_header_self_sufficiency` / `ludus_foundational_includes`
gates and by `./scripts/check`, not just by convention.

**Allowed everywhere (may be assumed available via `core.h`).**
`#include <ludus/foundation/base/core.h>` is the single foundational header. It
guarantees a translation unit has: the fixed-width types (`uint32`, `usize`,
`float32`, …); the build/OS-family/CPU-arch/compiler configuration macros
(`LUDUS_BUILD_*`, `LUDUS_PLATFORM_*`, `LUDUS_ARCH_*`, `LUDUS_COMPILER_*`); the
codegen macros (`LUDUS_INLINE`/`NOINLINE`/`COLD`/`LIKELY`/`UNLIKELY`/
`DEBUG_BREAK`/`UNREACHABLE`); `Move`/`Forward`/`DerivedFrom`/`nullptr_t`; and the
assertion entry points (`LUDUS_ASSERT`/`REQUIRE`/`CHECK`/`FATAL`). A file that
needs only part of this may include the specific Band 0 header instead
(`types.h`, `config.h`, or `compiler.h`).

**Required explicitly (include the header that declares what you name).**
Everything above the foundation is opt-in: containers (`vector.hpp`,
`array.hpp`), `pointer.hpp` (`UniquePtr`), strings/`<string_view>`, logging,
profiling, platform, graphics, math, threading. Do **not** rely on these
arriving transitively. `core.h` is what you *may* rely on; it is not mandatory
boilerplate — if a file already includes exactly what it uses, do not add
`core.h` just for cohesion.

**Forbidden in the foundational headers.** `core.h`, `config.h`, `compiler.h`,
and `types.h` must never include a string/container header (Ludus or std),
`<string_view>`, `<memory>`, `<span>`, a heavy STL header (`<format>`,
`<chrono>`, `<filesystem>`, `<regex>`, `<iostream>`, …), or any `ludus/` header
outside `foundation/base`. More generally, no public header may include a heavy
STL header (type-erase behind a `.cpp`; ADR 0004) or a private `internal/`
header.

**Include ordering.** In an engine header, include `core.h` (or the specific
Band 0 header you need) first; then other Ludus headers grouped by layer,
most-foundational first; then standard-library headers; then third-party
headers. Separate the groups with a blank line.

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

### Game project setup checks

When creating, loading, updating, or repairing a game project, check its CMake
setup as part of the work. Hidden base presets alone are not a usable setup.
Verify selectable configure/build/test presets for the supported profiles,
their inheritance and conditions, SDK/dependency prefixes, compiler, Ninja,
shader tools when required, and the IDE's CMake executable/preset mode. Use the
selected CMake's `--list-presets=configure`, `--list-presets=build`, and
`--list-presets=test`; parsing JSON alone does not prove preset availability.

Loading must report missing or stale setup without downloading, configuring,
building, or rewriting files. Explicit setup/update/repair should regenerate
owned local settings, preserve custom presets and unrelated editor settings,
refresh stale CMake caches when SDK/toolchain inputs change, and verify a real
configure/build/test before reporting success. Keep machine paths in ignored
local files such as `CMakeUserPresets.json`. Add regressions for a fresh clone,
missing local presets, moved tools/SDKs, and repeated repair. Future CLI/Editor
project operations must share these checks (see the project-sdk-workflow spec).

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
