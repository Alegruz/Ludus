# Associative container benchmark

Use the pinned toolchain and an optimized native preset:

```sh
out/host-tools/venv/bin/cmake --preset linux-clang-development -DLUDUS_BUILD_CONTAINER_BENCHMARKS=ON
out/host-tools/venv/bin/cmake --build --preset linux-clang-development --target ludus_container_bench
out/build/linux-clang-development/tools/container-bench/ludus_container_bench
```

The standalone tool compares SortedMap with standard unordered/tree map baselines.
It reports median of seven samples in nanoseconds per operation. Lookup uses
100,000 identical deterministic random queries (about half misses), separately
from setup. Insertion uses 500 repetitions of descending keys, retaining capacity
between batches. It includes clearing in both insertion measurements; the hash
baseline retains buckets but allocates/frees nodes. SortedMap's reported storage
is backing capacity times Entry size, excluding its 24-byte owner. The standard
maps' allocator/bookkeeping overhead is not estimated.

Record CPU, compiler, flags, library, build flavor, and system load with results.
Measure actual consumer cardinality too. The worst-case insertion workload is
intentional: sorted storage may lose badly at large cardinalities. This harness
is neither a general hash-table competition nor a CI timing gate. Only use a
result to support the workload it measured. Array's separate benchmark remains
unchanged; baseline standard containers are confined to measurement tools.
