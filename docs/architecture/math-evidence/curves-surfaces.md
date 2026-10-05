# Cubic and bicubic kernel evidence (G1)

Implementation date: 2026-10-05. Base: `95683aa`.
Scope: `cubic.hpp`, `patch.hpp` in `Ludus::FoundationMath`; no additional module
dependencies, prepared assets, mesh/collision/render adapters or editor binding.
See the [architecture](../curves-surfaces.md) and
[consulted-source review](../curves-surfaces-gems-review.md).

The implementation uses fixed stack storage, de Casteljau interpolation,
analytic derivative control nets and float64 intermediates. Patch samples reuse
their V rows across the six outputs. All public operations are exception-free
and `noexcept`. Float32 output narrowing checks range before committing results.
This is the readable CPU reference implementation; SIMD/GPU/prepared polynomial
alternatives and integrated performance budgets remain unmeasured.

## Numerical contract and tests

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

## Validation record

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

## Attribution

Reference comments near subdivision, Hermite conversion and patch normals thank
Philip J. Schneider, Thomas Lowe and Martin Brownlow and identify the work,
book/chapter/pages, adopted idea and departures. The review links the consulted
book pages and distinguishes actual chapter reading from index-only leads.
No article source code was copied; length bounds, root isolation, transported
frames and approximate shading fields are not implemented in this phase.
