# Memory Phase 0A: Aliased Array Resize

Implemented against baseline `d4a84ca`. This is the first approved memory-roadmap
step: repair `Array::Resize`/`TryResize` aliased fill across growth, preserving the
public API, layout, allocation family and capacity policy. It corresponds to
Phase 0A in the approved workspace `docs/architecture/memory-management.md` and
finding C4 in `docs/architecture/memory-management-review.md`. Those design files
were uncommitted in the source workspace at this baseline; this implementation
change restates its acceptance contract here and does not bundle that larger
architecture-document change.

## Implemented

Previously, `values.Resize(newSize, values[index])` could free or move from the
fill object before copying it into the new tail. The new regression reproduced
an ASan heap-use-after-free before the fix.

[`TryResizeImpl`](../../modules/foundation/containers/include/ludus/foundation/containers/array.hpp)
now obtains replacement storage, constructs the added elements while the fill
reference remains valid, relocates the existing elements, releases their old
storage and commits the new representation. With spare capacity it constructs
in place. Shrink and same-size requests do not consume the fill value.

## Public API

No new API, allocator, tag, instrumentation or ABI change. All four existing
`Resize`/`TryResize` overloads still share the private implementation. `Array`
remains three words (24 bytes on this host); `StaticArray` is unchanged.

## Internal architecture

The implementation uses the existing `AllocateStorage`/`FreeStorageBytes` seam
and centralized element-lifetime helpers. It neither copies a temporary fill
object nor scans/computes an index to detect aliasing. Allocation/size rejection
precedes element construction and state mutation. The existing exception-free
element-operation contract remains in force; no exception rollback is added.
No extra allocation or new allocation family is introduced.

## Tests

Seven new Catch2 cases exercise 37 generated combinations: both public fill
APIs; first/middle/last-element aliases; growing and reserved storage; nontrivial
values with lifetime/canary accounting; 128-byte alignment; external fill with
empty/populated storage; same-size/shrink/zero requests; default-initialized
tails; and overflow rejection without mutation. The existing move-only,
relocation, ownership, layout and allocation-count tests remain active. Fill
resize requires copying; this phase does not expand element-type constraints.

| Configuration | Build and test result |
| --- | --- |
| Debug | Warning-clean; 19/19 CTest tests passed |
| Development | Warning-clean; 18/18 passed |
| Release, tests explicitly enabled | Warning-clean; 17/18 passed initially; the sole assertion watchdog timeout passed on isolated retry |
| Profile | Warning-clean; 18/18 passed |
| ASan/UBSan | Warning-clean; 15/16 passed initially; the sole assertion watchdog timeout passed on isolated retry |

The failures were `ludus_assert_blocked_output`'s five-second watchdog during
parallel configuration runs, outside the changed container code. Isolated
reruns passed in 3.77 s (Release) and 4.15 s (sanitized). No watchdog limit was
increased and no assertion implementation was changed. The sanitizer container suite
passed all **3,543 assertions in 57 cases**, including the formerly failing case.

Pinned clang-format 18, the repository's full configured-source clang-tidy 18
check, standalone benchmark clang-tidy, foundational include and standalone
header gates passed. The existing benchmark helper's two count parameters have
a narrow lint annotation documenting their existing order; its unused barrier
helper was removed to allow a warnings-as-errors standalone build. Installed
Development SDK validation and its consumer passed. The time-traced build
budget passed: Array averaged **104 ms** per include against 2,000 ms; total
frontend parsing was **75.4 s** against 120 s.

The isolated worktree reused cached pinned tools/dependencies. The scripts'
bootstrap fingerprint check rejected that shared cache as stale; the same
repository format/include/tidy and SDK verification functions were run directly
against fresh CMake configurations. No dependency bootstrap metadata or recipe
was changed. Every engine configuration used C++23, `-fno-exceptions`, PCH off
and warnings-as-errors; test targets retained their normal exception setting.

## Benchmarks

Extended the [existing benchmark](../../modules/foundation/containers/benchmarks/array_bench.cpp)
with two fill-resize workloads and more output precision; no new harness.
Each processes 4,096 arrays, growing 64 initialized `uint32` elements to 1,024,
with capacity either reserved up front or grown. Both verify prefix/tail values
and retain the existing optimization barrier. Construction, allocation and
cleanup are included. External fill makes both revisions valid; timing the
old dangling alias would be meaningless.

Clang 18.1.3/libstdc++, C++23, `-O2 -DNDEBUG -fno-exceptions`, system allocator,
Intel i5-8265U, CPU 0 affinity, no sanitizers/capture. Both binaries use the same
benchmark source and support library; the baseline header is from `d4a84ca`.
After one warmup each, 12 fresh-process pairs ran in randomized order (seed 10).
Each process retains the existing harness's best-of-seven batch policy. Results
below are medians of those process minima, **not individual-operation p99s**.

| Workload | Before ns/element | After ns/element | Change |
| --- | ---: | ---: | ---: |
| Fill resize, growing | 0.1227 | 0.1200 | -2.2% |
| Fill resize, reserved | 0.1116 | 0.1099 | -1.5% |
| Reserved append control | 0.8144 | 0.8120 | -0.3% |
| POD32 append control | 36.4350 | 36.4909 | +0.2% |

The affected workloads' interquartile ranges overlap: growing was
0.1189–0.1256 before and 0.1174–0.1310 after; reserved was 0.1069–0.1130 before
and 0.1076–0.1149 after. The unchanged POD copy workload varied by -16.4%
(2.4520 → 2.0487), exposing environmental/code-layout sensitivity. **No speedup
claim is justified.** This evidence shows no clear resize regression at this
scale; it does not establish frame-time or allocator improvements.

Whole-benchmark median peak RSS was 15,060 KiB in both variants. This aggregate
includes all workloads and is not a per-resize fragmentation measurement.
[Raw samples](memory-phase-0a-evidence/benchmarks.csv) and
[metadata, hashes and process measurements](memory-phase-0a-evidence/metadata.json)
retain all ten workloads, including unfavorable/noisy observations. Final
benchmark binaries were byte-identical to the measured binaries after the
comment-only lint annotation.

To reproduce, build the existing benchmark with the flags above and the Release
Base library/generated includes. Compile the same benchmark source twice, once
with the baseline `array.hpp` in a preceding include overlay, once with the
current header. Keep the other sources, includes, compiler and library fixed;
run each on one CPU in randomized fresh-process pairs. The normal CMake presets
and `ctest --test-dir out/build/<preset> --output-on-failure` reproduce the tests;
configure Release with `-DLUDUS_BUILD_TESTS=ON`.

## Repository audit

Repository searches covered resize call sites, raw byte allocation families,
placement construction and ordinary new/delete in maintained source. Every
resize overload reaches `TryResizeImpl`; it still uses the single existing
Array byte seam. There is no FoundationMemory/AllocationDomain API yet and no
bypass or owning-pointer-family migration in this phase.

Intentionally retained paths are Array's aligned nothrow new/aligned delete,
Base UniquePtr's matching default delete, logger/sink and window factories,
profiling's current calloc/free slab, standard-library internals, native-library
destroy pairs and isolated allocation-test interposers. Their migrations and
known defects belong to later approved steps. No legacy allocator was replaced,
so none was removed or left competing with a new implementation.

## Known limitations

The first phase is a container correctness prerequisite, not an allocator
implementation. Arbitrary throwing or side-effecting element constructors are
outside its unchanged contract. Forced backend-OOM/self-move tests and the
broader element-type contract remain Phase 0B. Ownership-helper and profiler
lifetime/alignment findings remain separate work. Windows/macOS are not
build-supported by this baseline; no new threading guarantee is introduced.

## Next phase

**Phase 0B:** test and repair the fallible element contract, especially self-move
`TryAdd` with forced allocation failure and supported copy/move/relocation
constraints. Do not implement FoundationMemory until its approved prerequisites
and baseline gates are met. Phase 0B is not included here.
