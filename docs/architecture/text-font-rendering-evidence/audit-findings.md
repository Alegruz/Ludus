# Text/font rendering — audit findings (T01–T16 / F0–F6)

Audit of the implemented code against the committed spec. Checkboxes were
treated as unproven; every claim below was re-verified against the actual source
and re-run with the pinned toolchain. No GPU/web mocks were substituted for the
real-GPU gates.

## Scope present in the implementation

Only the CPU Text module (`Ludus::Text`) exists: font loading, UTF-8 validation,
HarfBuzz shaping, FreeType metrics, and FreeType grayscale rasterization. There
is **no** GraphicsText module, **no** RHI `coverage_quads.h`, **no** Vulkan or
WebGPU text pipeline, **no** atlas, demo, or web/wasm build. The audit therefore
covers F0, F1, and the rasterization slice of F2; everything from the atlas
onward (F2 remainder, F3–F6, and all of T08–T13) is unimplemented, not merely
unverified.

## Verified true (re-run this audit)

| Area | Requirement | Evidence |
| --- | --- | --- |
| Native dependency options/parity | T02 | `libfreetype.a` has no PNG/zlib/Brotli symbols; `libharfbuzz.a` exports `hb_ft_font_create_referenced` and has no subset/ICU/GLib symbols; versions are FreeType 2.14.3 / HarfBuzz 14.5.1. |
| Font/source hashes | T02 | All three committed fixture SHA-256s and the FT/HB source hashes match `dependency-manifest.md` byte-for-byte. |
| Strict UTF-8 | T04 | Overlong, bad-continuation, surrogate, >U+10FFFF, stray-continuation, truncated cases all rejected with first-invalid-byte offset. |
| hb-ft state / alternating sizes | T04/T05 | `SyncFaceSize` re-sizes + `hb_ft_font_changed` + load flags; test shows 16/48/16 px metrics do not contaminate, and rasterization is byte-identical across a size round trip (added this pass). |
| Shaped positions/clusters/RTL | T04 | Clusters are original UTF-8 byte offsets; RTL Arabic preserves HarfBuzz visual order; signed 26.6 advances/offsets copied verbatim; kerning reflected in `AV` advance (added). |
| Measurement, spaces, empty, .notdef | T05/T06 | Baseline-relative ink/advance/ascent/descent/line-advance; empty is a valid empty layout; spaces keep records without ink; `.notdef` counted. |
| Normalized coverage / bearings | T07 | Signed pitch normalized to top-down tightly-packed R8; non-256-gray and color/LCD rejected; zero-area glyphs valid; negative left bearing (`j` → -3 at 48px) reported faithfully (added); AA partial values present (added). |
| Ownership/limits/handles | T03/T14 | Generation handles, bounded fonts/layouts/bytes/input, `Busy` on unload-with-layouts, stale/invalid handles rejected, counters. |
| SDK private-header + link closure | T15 | `cmake --install` exports only the two public headers (no `utf8.h`/`ft2build.h`/`hb*.h`); `Ludus::Text` records `$<LINK_ONLY:Freetype::Freetype>` and `$<LINK_ONLY:harfbuzz::harfbuzz>`. |
| Regressions | — | Keyboard, platform-headless, RHI, RHI-lifecycle, and smoke-simulation tests all pass; no source outside `modules/text` + build wiring changed. |

Pinned-check results this pass: `linux-clang-development` build + 25/25 tests
(text: **39 cases / 285 assertions**), `--format` and `--tidy` clean (0 project
errors), header self-sufficiency + foundational includes pass, build-budget
passes, `linux-clang-asan-ubsan` text tests clean (no leaks/UB).

## Gaps and fixes made in this audit

1. **Missing negative-bearing / combining-mark / determinism tests** (F1/F2
   ledger had flagged these as "still to add"). *Fixed:* added
   `modules/text/tests/audit_tests.cpp` with real-font cases for a negative left
   bearing, a combining accent, deterministic size-stable coverage, anti-aliased
   coverage values, and kerning-reflected advances. Ledger updated.

## Minor findings (not fixed; low risk, documented)

* **Empty `$<LINK_ONLY:>` entries in the exported Text interface.** The FreeType
  Conan target contributes two empty link items, which propagate into
  `Ludus::Text`'s `INTERFACE_LINK_LIBRARIES`. They are dropped at link time and
  do not affect the real FT/HB closure; the root cause is the vendored FreeType
  package definition, not the Ludus target. Left as-is to avoid masking the
  genuine closure; revisit if a consumer's CMake flags it.
* **`using uint16` unused in the public header.** Harmless; a `using` alias emits
  no warning. Left to avoid header churn.
* **Control-validation loop depends on prior UTF-8 validation.** `ShapeInternal`
  re-decodes to check controls/script and would stall on a zero-length decode;
  this cannot occur because `ValidateUtf8` runs first and rejects invalid input.
  Safe today; a defensive guard would be a cheap hardening if the ordering ever
  changes.

## Pending gates (kept pending — no mocks, no invented numbers)

* **Real Linux/Vulkan and browser/WebGPU images (T01, T12, T13, T16; F3–F6).**
  No GPU/Vulkan loader and no working Emscripten SDK in this environment (`emsdk
  install` for the pinned 4.0.23 fails unpacking node because `xz` is absent).
  No GPU verification is claimed; these tasks remain unchecked.
* **Atlas/upload/frame limits, UVs, clipping, painter order, sRGB formats,
  premultiplied blending, warm allocations, in-flight resources, async callbacks,
  device recovery (T08–T13).** Unimplemented — belongs to GraphicsText + RHI,
  which do not exist yet. Nothing to verify; correctly left unchecked.
* **wasm dependency/font/CPU parity (T02/T16).** Pending with the web toolchain.
