# Text/font rendering — F0–F2 native CPU results and environment report

This records what was actually built and verified for the CPU Text module
(`Ludus::Text`) on the native Linux toolchain, and the concrete environment
limits that leave the GPU and web gates pending.

## Build/test environment (this sandbox)

* OS: Amazon Linux 2023 (not the Ubuntu 24.04 reference host).
* Reference toolchain requirement: Clang/LLVM 18 with LLD. The base image ships
  Clang/LLD **15**, which the project bootstrap correctly refuses. LLVM/Clang
  **18.1.8** and `lld18` were installed from the AL2023 `dnf` repositories to
  satisfy the pinned-toolchain gate. CMake 3.29.6 / Ninja 1.11 / Conan 2.8.1
  were installed by the project's own venv bootstrap.
* Two environment-only toolchain shims were needed and do **not** modify the
  repository:
  * `ld.lld-18`, `clang-18`, `clang++-18`, `clang-format-18`, `clang-tidy-18`
    symlinks on PATH so the bootstrap's version detection finds v18.
  * A thin `clang-tidy-18` wrapper that hoists the version onto `--version`
    line 1 (the AL2023 clang-tidy prints the LLVM banner first, defeating the
    first-line version parser). It is otherwise transparent.
  * `gcc14-libstdc++-static` was installed so Conan's from-source tool builds
    (ninja) can statically link libstdc++ as they request.
* **No GPU / Vulkan / Wayland / Emscripten.** `/dev/dri` is absent, there is no
  Vulkan loader, no compositor, and `emcmake`/the Emscripten SDK are not
  present. Platform builds its headless window backend (native content scale is
  1.0 and reported as such).

## F0 — dependencies, assets, build seams (DONE natively)

* `conanfile.py` now requires `harfbuzz/14.5.1` (and configures `freetype`
  options); `third_party/harfbuzz/conanfile.py` builds the pinned HarfBuzz from
  verified source with the design's option set; `engine.py` exports local
  recipes before locking. See `dependency-manifest.md` for URLs/hashes/licenses.
* Both pinned libraries build and package statically through Conan with engine
  exceptions disabled; `conan.lock` records the revisions. Reconfigure + rebuild
  work offline after bootstrap (Conan cache is populated).
* `Ludus::Text` target and public header file-set added; FreeType/HarfBuzz are
  private, consumed as SYSTEM includes (project warning/tidy policy not applied
  to vendored headers).
* Fixture fonts (Latin / Arabic / subset Hangul) packaged with OFL licenses and
  verified hashes; tests load them with no system-font or network assumption.

### Recorded integration correction

The FreeType 2.14.3 ConanCenter package consumed here does not propagate its
`include/freetype2` directory through the imported target's interface includes
(the generated `freetype_INCLUDE_DIRS_*` is empty although the recipe appends
it). `modules/text/CMakeLists.txt` resolves `ft2build.h` via `find_path` seeded
with the per-config package folder Conan does record, and attaches it as a
SYSTEM include. This is a build-wiring correction with evidence, not a change to
the selected architecture.

## F1 — owned fonts, shaping, metrics (DONE natively)

Implemented in `modules/text`:
* `FontSystem` owner, generation-checked `FontId`/`LayoutId` (20-bit slot +
  12-bit nonzero generation, wrap-retire), bounded owned font bytes, bounded
  layouts with copied source UTF-8, fallible cleanup, idempotent shutdown, and
  `Busy` on unload with live layouts / destroy with a referencing renderer.
* Strict UTF-8 validation (overlong, bad continuation, surrogates, >U+10FFFF,
  first-invalid-byte) ahead of HarfBuzz.
* Control/script validation for the single-run contract (rejects LF/CR/TAB/NUL
  and bidi paragraph controls; rejects strong-script scalars inconsistent with
  the declared script; allows Common/Inherited, ZWJ/ZWNJ, variation selectors).
* hb-ft shaping with `HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES`, identical
  `FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP` load flags, `hb_ft_font_changed`
  on size change, preserving glyph IDs, signed 26.6 advances/offsets, and
  original UTF-8 byte clusters. RTL output order is preserved; no post-shaping
  kerning and no UTF-8 reversal.
* Baseline-relative ink bounds, advance, ascent/descent, line advance in
  physical pixels; empty text is a valid empty layout; spaces keep glyph records
  without ink; `.notdef` is reported via a missing-glyph count.

## F2 — grayscale rasterization (partial: raster DONE; atlas pending)

* `RasterizeGlyph` renders light-hinted `FT_RENDER_MODE_NORMAL` grayscale,
  normalizes signed pitch into top-down tightly-packed R8 coverage, rejects
  non-256 gray ranges and non-grayscale (color/LCD) formats explicitly, treats
  zero-area glyphs as valid no-image metrics, and lends scratch valid only until
  the next raster call. The bounded append-only atlas / cache (GraphicsText) is
  **not** implemented in this pass.

## Verification run (commands and results)

| Check | Command | Result |
| --- | --- | --- |
| Native development build | `scripts/build linux-clang-development` | PASS (warning-clean) |
| Native tests | `scripts/test linux-clang-development` | PASS 25/25 (text = 34 cases / 230 assertions) |
| Format | `scripts/check linux-clang-development --format` | PASS |
| Static analysis | `scripts/check linux-clang-development --tidy` | PASS (0 project errors) |
| Headers / foundational includes | ctest `header_self_sufficiency`, `foundational_includes` | PASS |
| Build-time budget | `scripts/check-build-budget` | PASS (5 headers within budget) |
| Sanitizers | `scripts/build/test linux-clang-asan-ubsan` | PASS (text clean, no leaks/UB) |

Baseline before this work: 24/24 tests green at `main` (`3f2dca3`). No keyboard,
RHI, smoke, or probe behavior was changed.

## Pending gates (not satisfiable in this environment)

* **F3 real Vulkan + WebGPU coverage-quad pipelines** — needs a GPU/Vulkan
  loader and an Emscripten/WebGPU browser. Neither is present.
* **F0/F1/F2 web parity** — needs the pinned Emscripten SDK + web source
  bootstrap (not installed here).
* **F4–F6 GPU text, demo GPU output, real-image gates, web export consumer** —
  all depend on the above.

These are reported pending and their tasks/ledger rows are left unchecked, per
the handoff's instruction to finish independent work and not claim GPU/web
verification from an environment that cannot provide it.
