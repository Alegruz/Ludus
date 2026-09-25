# ADR 0007: The Foundational `core.h` Header and Include Boundary

## Status

Accepted.

## Context

Ludus had a *de facto* universal vocabulary — the fixed-width type aliases in
`ludus/foundation/base/types.h` — but its universality was accidental: `types.h`
was included directly by only two `.cpp` files yet reached ~every translation
unit **transitively**, pulled in by ~28 other headers. Nothing stated, in one
place, what a Ludus source file may assume is available, and a routine header
refactor could break unrelated files. In parallel:

- `defines.h` and `defines.hpp` were near-duplicates (both defined
  `LUDUS_INLINE` and `nullptr_t`; `defines.hpp` additionally carried the
  build-flavor macros and was missing from the CMake `FILE_SET`);
- OS/platform detection lived *above* the foundation, in the platform
  (windowing) module, so "am I on macOS?" required depending on a window
  library — a blocker for the planned macOS / Vulkan / Metal / D3D12 work;
- there was no PCH and, more importantly, no policy to stop a future PCH from
  silently becoming the dependency model.

The full audit, alternatives, and trade-offs are in
`docs/architecture/foundational-headers.md`.

## Decision

Adopt a two-tier ("Candidate D") model with an explicit, contractual foundation
and everything else opt-in.

1. **Band 1 — `ludus/foundation/base/core.h` is THE foundational header.** It is
   the one header a translation unit may rely on being universally available. It
   provides the fixed-width types, the build/OS/arch/compiler configuration
   macros, the codegen attribute/hint macros, `Move`/`Forward`/`DerivedFrom`/
   `nullptr_t`, and the assertion entry points — and nothing heavier. `core.h`
   is what a file *may* rely on; it is not mandatory boilerplate, and a file that
   needs only part of the foundation may include the specific Band-0 header.

2. **Band 0 — macros only, no code.** `config.h` owns OS-family, CPU-arch, and
   build-flavor detection (moved down from the platform module); `compiler.h`
   owns compiler identity and the attribute/hint macros. `defines.h`/
   `defines.hpp` are removed and `compiler.hpp` renamed to `compiler.h`.

3. **Band 2 — everything else is explicit.** Containers, strings, logging,
   profiling, math, threading, filesystem, and smart pointers beyond the core
   are included by the files that use them. No reliance on accidental transitive
   includes above the foundation line (Chromium/Abseil discipline).

4. **PCH is a build accelerator only.** `LUDUS_ENABLE_PCH` (default OFF)
   precompiles exactly `core.h`. The default build (PCH off) is the guarantee
   that no file depends on a PCH-only symbol; a CI job exercises the PCH-on path.

5. **Mechanical enforcement.** A header-self-sufficiency compile gate (every
   public header compiles standalone) and a narrow include-boundary gate (the
   foundational headers pull no strings/containers/heavy STL; no public header
   pulls heavy STL or a private header) run in CI and from `./scripts/check`.

## Consequences

The engine now has a single, documented answer to "what is always available,"
decoupled from transitive luck; portability decisions have a home at the
foundation; the duplicate/mis-packaged `defines` headers are gone. Build health
is preserved: `core.h` is cheap (only `<cstdint>`/`<cstddef>`/`<type_traits>`/
`<utility>` plus the assertion policy header), the heavy facilities stay opt-in
and budgeted (ADR 0004/0005), and the boundary is enforced rather than merely
documented.

This reinforces, and does not replace, ADR 0003 (standard-library policy),
ADR 0004 (type-erase heavy templates), and ADR 0005 (build-time budgets). It is
forward-compatible with a later C++20 modules migration: `core.h` is the natural
first module interface unit and the Band-2 explicit-include discipline maps onto
`import` boundaries. `AGENTS.md` carries the day-to-day Allowed / Required
explicitly / Forbidden summary.
