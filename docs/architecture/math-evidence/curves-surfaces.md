# Curves and surfaces evidence (G0-G1)

This ledger records the G0 contract freeze and reference corpus, then the G1
cubic and bicubic kernel. See the [architecture](../curves-surfaces.md),
[ADR 0024](../../decisions/0024-curves-surfaces-contracts.md) and the
[consulted-source review](../curves-surfaces-gems-review.md).

## G0 contract freeze and reference corpus

Implementation date: 2026-10-15. Base: `5117c8c`. Scope: ADR 0024, the
test-only corpus in `tests/fixtures/curves_surfaces/`, a FoundationMath
cross-check test and the boundary audit below. No Geometry module, prepared
owner, cache, codec or editor binding is added.

### Reference corpus

`generate_fixtures.py` uses only the Python standard library. Polynomial
samples, derivatives, Catmull-Rom conversion, track semantics, shared edges and
tessellation bounds use exact rationals. Real roots (zero-speed parameters and
projection stationary points) are isolated with Sturm sequences and bisected to
2^-180. Arc lengths split at zero-speed roots and integrate the exact speed with
50-digit tanh-sinh quadrature; distance inversion uses safeguarded Newton with a
residual below 1e-38. Each value is rounded to float64 once. Controls and track
keys are exact float32 values.

`test_fixtures.py` (CTest `ludus_geometry_reference_fixtures`) checks the
oracles without Ludus code:

- Quadrature agrees with closed forms to 1e-35: lines, the ease line, the cusp
  `2(5 sqrt 5 - 8)`, the parabola `3(sqrt 5/2 + asinh(2)/4)` and the reversing
  line's total variation at roots `(5 -+ sqrt 5)/10`.
- Projection ties are certified by exact polynomial identities: mirror symmetry
  for the arch queries and `|C(u)-Q|^2 - 1 = (x(u) - 1/2)^2` for the three-way
  tie on the reversing line. Global minima also hold on a 2049-point rational
  grid.
- Catmull-Rom Bezier controls equal the Barry-Goldman pyramid exactly at 17
  parameters; knots are 0, 1, 3, 6.
- Saddle and bulge tessellation maxima equal 9/4 and 27/16, are attained, and
  never exceed the control-net plus twist bound on a 33 x 33 grid.
- Track samples follow the frozen span-side and boundary-policy rules; the
  Hermite overshoot is 43/27 at 5/3 s.
- The committed header matches the generator, ignoring only whitespace and
  formatter-inserted trailing commas.

`curves_surfaces_fixture_tests.cpp` in `ludus_foundation_math_tests` checks the
G1 kernel against the header. Endpoints, corners and origin-relative controls
must match bitwise (Exact). Interior samples, derivatives, Hermite controls and
normals must fall inside an estimated envelope of one float32 ulp of the larger
of the reference magnitude and the control scale (36x for derivatives). This is
a regression bound for Rounded results, not a certified error. Dense chord
polygons stay within the reference arc lengths; dense samples never beat a
projection reference; the triangle-pair deviation never exceeds the tessellation
bound; normal classifications match `TryPatchNormal`.

### Numeric guarantees versus estimates

ADR 0024 assigns every planned result a class. G1 results are Exact at stored
endpoints and corners and otherwise Rounded. No Certified helper exists, so no
strict profile is available yet. The float32 position floor `2 * ulp32(E)`
follows from half-unit storage plus half-unit output rounding within the convex
hull, at most sqrt(3) units per point.

### Boundary audit

| Boundary | Finding | Action |
| --- | --- | --- |
| Math dependencies | `ludus_foundation_math` links only `Ludus::FoundationBase` | None |
| Kernel headers | `cubic.hpp` and `patch.hpp` include only `core.h`, `status.hpp` and `vector.hpp`; neither is in `core.h` or the foundational PCH | None |
| Kernel consumers | Only Math tests, the allocation probe, the Wasm math probe and the SDK consumer include them; no G1 value is serialized | None |
| Public API documentation | 60 baseline entries for cubic and patch structs, members and overloads lacked Doxygen | Documented each declaration and removed the entries from `docs/api-undocumented.json` |
| Parameter naming | `TrySplitCubic` output references were named `left` and `right` | Renamed to `outLeft` and `outRight`; source compatible |
| Cooked format owner | The architecture placed the cooked format inside GeometryContent, which conflicts with the rule that formats shared by tools and runtime have their own header-only target | ADR 0024 assigns it to a Base-only `Ludus::GeometryFormat` |
| Fixture header | Test-only INTERFACE target linking Base and Containers; not installed or exported | None |

### G0 validation record

Host: macOS 26 on arm64 with the pinned host tools (Clang/LLVM 18.1.8, CMake
3.29.6), preset `macos-clang-development`, warnings as errors.

| Check | Result |
| --- | --- |
| Reference corpus self-checks (`ludus_geometry_reference_fixtures`) | Passed: 12 tests |
| G1 cross-check against the corpus (`[fixtures]`) | Passed: 8 test cases, 87,452 assertions |
| Full FoundationMath unit tests | Passed: 107 test cases, 420,733 assertions |
| FoundationMath allocation probe and foundational include gate | Passed |
| Header self-sufficiency | Passed: 153 public headers; run directly because the loaded host exceeded the 120 s CTest limit |
| Pinned clang-tidy on changed translation units | Passed: `cubic.cpp`, `curves_surfaces_fixture_tests.cpp` |
| Formatting of changed C++ sources | Passed: `scripts/check --format --fix`, then repository formatter rerun unchanged |
| Changed-line source policy | Passed against `5117c8c` |
| Docs index, strict MkDocs and wiki checks | Passed |
| API reference with pinned Doxygen | Passed: no new gaps; baseline only shrinks against `origin/main` |
| ASan/UBSan | Built warning-clean; execution blocked by the documented local Clang 18 ASan startup deadlock on macOS 26, so the CI sanitizer lanes remain required |
| Wasm, SDK consumer | Not rerun: the kernel's arithmetic and exported signatures are unchanged; parameter renames and comments do not affect the Wasm corpus or installed consumer |

```sh
python3 tests/fixtures/curves_surfaces/generate_fixtures.py --check
./scripts/build macos-clang-development --target ludus_foundation_math_tests ludus_math_alloc_tests
out/host-tools/venv/bin/ctest --test-dir out/build/macos-clang-development \
  -R 'ludus_foundation_math_tests|ludus_math_alloc_tests|ludus_geometry_reference_fixtures|ludus_foundational_includes' \
  --output-on-failure
python3 tests/build_contract/verify_header_self_sufficiency.py out/build/macos-clang-development
python3 scripts/python/source_policy.py --base 5117c8c
```

The generator acknowledges H. Takahasi and M. Mori (tanh-sinh quadrature),
P. J. Barry and R. N. Goldman (Catmull-Rom pyramid) and C. Yuksel, S. Schaefer
and J. Keyser (centripetal parameterization) in its module comment. No source
code from those works is copied.

## G1 cubic and bicubic kernel

Implementation date: 2026-10-05. Base: `95683aa`.
Scope: `cubic.hpp`, `patch.hpp` in `Ludus::FoundationMath`; no additional module
dependencies, prepared assets, mesh/collision/render adapters or editor binding.

The implementation uses fixed stack storage, de Casteljau interpolation,
analytic derivative control nets and float64 intermediates. Patch samples reuse
their V rows across the six outputs. All public operations are exception-free
and `noexcept`. Float32 output narrowing checks range before committing results.
This is the readable CPU reference implementation; SIMD/GPU/prepared polynomial
alternatives and integrated performance budgets remain unmeasured.

### Numerical contract and tests

Parameters are finite in `[0,1]`. Controls/results are float32; interpolation
uses float64. Endpoints/corners preserve the stored values, including signed
zero. All failed calls preserve every output. Full samples fail when a partial
or derivative exceeds float32 range, while the position-only overload can still
succeed. Split controls are rounded, so shape equivalence is numerical rather
than symbolic. No interval enclosure, certified length, distance inversion or
global projection guarantee is provided by G1.

`bezier_tests.cpp` uses independently written Bernstein basis polynomials and
analytic basis derivatives, rather than the production de Casteljau/difference
net helpers. It compares 100 deterministic cubic control sets and 25 patch nets
at endpoints and interior parameters. The corpus also covers scalar powers,
2D/3D Hermite domain scaling, split parameter remapping/derivative scaling,
collapsed endpoint children, matched split controls, saddle mixed derivatives,
normal orientation, extreme finite/subnormal partials, nonfinite inputs, invalid
domains, range failures, supported input/output aliasing and unchanged outputs.
The existing allocation probe exercises conversion, sampling, normals and
subdivision under a counting allocator. SDK and Wasm probes call exported APIs.

### Validation record

Pinned native toolchain: Clang/LLVM 18, CMake 3.29.6, Ninja 1.11.1.3, Conan 2.8.1,
C++23, precise floating point (`-ffp-contract=off`), exceptions disabled in engine
targets, warnings as errors. Browser toolchain: Emscripten 4.0.23 from
`config/web_toolchain.json`.

| Check | Result |
| --- | --- |
| New deterministic kernel unit corpus | Passed: 8 test cases, 8,773 assertions |
| FoundationMath allocation probe | Passed: 27 assertions, zero allocations/deallocations |
| Full Development build/tests and public-header gates | Passed: 65 CTest entries, 2 live-display tests skipped |
| Debug math/allocation tests and public-header gates | Passed: 4 CTest entries, including the final normal regressions |
| ASan/UBSan build/tests | Passed: 57 CTest entries, 2 live-display tests skipped; changed kernel rechecked |
| Pinned format/tidy | Passed: format; full tidy scan's 3 affected TUs corrected and passed targeted recheck |
| Installed SDK consumer | Passed: headers installed, linked and executed |
| WebAssembly numeric corpus | Passed: Development and Release Wasm/Node |
| PR CI and mergeability | Tracked by live PR checks; required before ready for review |

Validation commands (after native `init.sh --locked --with-tests --preset-only`
and browser `init.sh --with-web-probes --preset-only` setup):

```sh
./scripts/build linux-clang-development
./scripts/test linux-clang-development
./scripts/build linux-clang-asan-ubsan
./scripts/test linux-clang-asan-ubsan
./scripts/check linux-clang-development --all
# Corrected the three reported TUs, then checked them with pinned clang-tidy:
out/host-tools/bin/clang-tidy -p out/build/linux-clang-development --quiet \
  --warnings-as-errors='*' modules/foundation/math/src/cubic.cpp \
  modules/foundation/math/src/patch.cpp modules/foundation/math/tests/bezier_tests.cpp
./scripts/check --format
./scripts/install-sdk linux-clang-development
./scripts/build linux-clang-debug --target ludus_foundation_math_tests ludus_math_alloc_tests
out/host-tools/venv/bin/ctest --test-dir out/build/linux-clang-debug \
  -R 'ludus_foundation_math_tests|ludus_math_alloc_tests|ludus_header_self_sufficiency|ludus_foundational_includes' --output-on-failure
./scripts/build web-emscripten-development --target ludus_web_math_probe
out/host-tools/venv/bin/ctest --test-dir out/build/web-emscripten-development \
  -R '^ludus_web_math_corpus$' --output-on-failure
./scripts/build web-emscripten-release --target ludus_web_math_probe
out/host-tools/venv/bin/ctest --test-dir out/build/web-emscripten-release \
  -R '^ludus_web_math_corpus$' --output-on-failure
```

After the helper-signature cleanup, rebuild/recheck the affected kernel tests in
Development, Debug and ASan/UBSan, plus both Wasm corpus profiles. These results
validate G1 only; the remaining architecture phases keep their own gates.

### Attribution

Reference comments near subdivision, Hermite conversion and patch normals thank
Philip J. Schneider, Thomas Lowe and Martin Brownlow and identify the work,
book/chapter/pages, adopted idea and departures. The review links the consulted
book pages and distinguishes actual chapter reading from index-only leads.
No article source code was copied; length bounds, root isolation, transported
frames and approximate shading fields are not implemented in this phase.
