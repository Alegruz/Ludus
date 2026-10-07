# First container-system increment: evidence

The [architecture](../architecture/container-systems.md) selects storage by
workload. This increment supplies a contiguous `SortedMap` and repairs fallible
Array insertion/append. It does not establish a general lookup speedup.

## Reproducible measurement

Use the optional [benchmark](tools/container-bench.md). Measurements
below used Clang 18.1.3, libstdc++ from GCC 13.3.0, C++23, x86-64 Linux, and the
Development preset (`-O2 -g`, assertions enabled, exceptions disabled). Hardware
was an Intel Core i5-8265U, four cores/eight logical CPUs. This task's builds and
analysis had finished before measurement; other desktop activity and CPU
frequency were not controlled. These are local observations, not CI thresholds.

Each lookup sample contains 100,000 identical seeded queries, about 50% misses.
Insertion samples contain 500 descending-key rebuilds, including clear, with
capacity retained. Each reported number is the median of seven samples. Two
successive harness runs are shown as a range of their medians; this is not a
confidence interval. Columns give nanoseconds per operation.

| Entries | Sorted lookup | Hash lookup | Tree lookup | Sorted rebuild | Hash rebuild | Sorted backing bytes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 10.32–11.75 | 10.26–11.17 | 4.89–5.21 | 5.60–5.61 | 24.35–38.20 | 16 |
| 4 | 20.16–20.31 | 14.68–14.91 | 14.47–14.54 | 9.05–9.31 | 24.61–25.16 | 64 |
| 16 | 30.37–31.38 | 13.54–16.33 | 19.19–19.89 | 12.90–13.17 | 28.57–30.26 | 256 |
| 64 | 44.72–44.79 | 14.92–17.11 | 28.58–30.98 | 17.71–18.50 | 33.17–42.74 | 1,024 |
| 256 | 61.97–65.21 | 13.53–15.63 | 35.61–36.94 | 33.42–39.23 | 33.08–37.27 | 4,096 |
| 1,024 | 70.57–71.86 | 15.31–16.09 | 42.29–43.58 | 93.97–103.28 | 35.34–39.73 | 16,384 |

Baselines are `std::unordered_map<uint64, uint64>` and
`std::map<uint64, uint64>`. Sorted storage excludes the 24-byte owning object.
Hash/tree allocator and bookkeeping bytes were not measured. Hash rebuilds
retain buckets but allocate/free nodes; sorted rebuilds retain their single
block. The workloads intentionally measure both lookup losses and mutation
costs. They do not cover string keys, adversarial collisions, cold caches,
deletion churn, other CPUs, or browser performance.

On this dataset, hashing generally wins lookup; sorted rebuilds win at 1–64
entries, overlap at 256, and lose at 1,024. There is no universal crossover
threshold. GameHost's supported clear-configuration fixture currently binds one
logical asset. Its migration is justified by explicit failure handling and
contiguous reusable storage; these timings do not demonstrate an engine frame
time improvement. Re-evaluate the selection when resource cardinality or the
access pattern changes.

## Deterministic correctness and allocation evidence

- 10,000 seeded map operations are compared after every mutation with an
  independent direct-index membership/value model, including ordered output.
- Lifetime ledgers verify insertion, relocation, copying, removal, clear, reset
  and destruction. Move-only and non-default-constructible values are covered;
  fallible Array append still supports non-assignable values.
- Failure injection rejects the existing nothrow allocation seam. Failed
  insertion and append preserve pointers, size, capacity and aliased/external
  move-only arguments; duplicate insertion consumes nothing. Oversize capacity
  requests fail without changing the map.
- Reserving 16 entries makes exactly one allocation. Inserting descending keys,
  finding each value, clearing and reusing capacity add none. Reset makes one
  matching free. The production allocator is not replaced or instrumented.
- GameHost tests check independent staged copies, updates, missing lookups and
  retirement. Capacity preparation precedes presenter commit; reload staging
  can reject allocation failure without changing the active registry.
- Both the installed native SDK consumer and the browser foundation probe
  instantiate the new public header and exercise fallible insertion. Timings
  are not asserted in tests.

Full warning-clean builds, unit tests, ASan/UBSan, formatting, pinned clang-tidy,
standalone headers, include boundaries and installed-SDK checks are the review
gates. Live Wayland/keyboard tests require a desktop session; local headless
runs report them as skipped. The PR's CI jobs supply the independent build
budget and browser/package validation. No budget is raised by this increment.
