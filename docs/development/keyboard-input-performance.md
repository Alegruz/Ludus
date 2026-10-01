# Keyboard input: performance evidence

Planning-grade wall-clock measurements for the backend-independent keyboard
reducer (spec design section 8, requirement K12). These are NOT a keyboard
hardware-to-photon latency claim and NOT a whole-engine determinism guarantee;
they establish that runtime storage and per-step work are bounded and that the
hot paths allocate nothing and take no locks.

## Environment

| Field | Value |
| --- | --- |
| Compiler | Clang/LLVM 18.1.8 (pinned reference toolchain) |
| Build preset | `linux-clang-development` (RelWithDebInfo, `-O2`) |
| CPU | Intel Xeon Platinum 8488C, 8 vCPU (sandbox) |
| OS | Amazon Linux 2023 (kernel 6.1) |
| Harness | `modules/input/benchmarks/input_bench.cpp` |
| Iterations / warmup | 2000 / 200 per workload; median and p95 reported |

The sandbox is a shared virtualized host, so absolute nanoseconds carry noise;
the shape (bounded, no allocations) is the load-bearing result.

## Storage footprint

| Component | Size |
| --- | --- |
| `sizeof(InputSystem)` public handle | 8 bytes (a `UniquePtr<Impl>`) |
| `Reducer<256,64,256>` (the heap-allocated `Impl`) | 16,136 bytes (15.8 KiB) |
| `InputDebugTrace` (optional) | 16,424 bytes (16.0 KiB) |
| Reducer + enabled trace | 31.8 KiB |

31.8 KiB with the trace enabled is well under the ≤128 KiB per-instance planning
target. The reducer storage is allocated exactly once (non-throwing `new`) at
construction; the trace is a separate caller-owned object.

## Allocations

`ludus_input_bench` counts global `operator new` calls:

- Construction: **1** allocation (the reducer `Impl`).
- Hot loop (5000 iterations of ingest/consume/query): **0** allocations.

The dedicated `ludus_input_alloc_tests` gate asserts the same (one at
construction, zero across 2000 hot iterations) and is part of the ctest suite.
No lock is taken on any path (single-thread, instance-owned; no atomics/mutex in
the reducer).

## Workload timings (median / p95, nanoseconds)

Representative run at `d8a2bbf`:

| Workload | Median | p95 |
| --- | --- | --- |
| Idle step (no events) | 66 | 66 |
| Typing/movement batch (6 transitions, button+2 axes) | 151 | 172 |
| Repeated/unknown batch (64 ignored/rejected records) | 214 | 313 |
| 256 transitions × 256 bindings (saturated) | 29,685 | 32,062 |
| Reset storm (16 resets + taps per step) | 380 | 412 |
| Trace off vs on (typing batch) | 85 | 118 (on) |

Notes:

- The saturated 256×256 case is the design's acknowledged worst case: step work
  is bounded by `events × bindings` with the simple flat binding scan. ~30 µs for
  a fully saturated step (far beyond any realistic keyboard batch) is acceptable
  for 64 actions; a per-key adjacency table is a documented later optimization
  only if profiling of real workloads shows it matters.
- Trace-on adds a bounded, constant per-record cost (no formatting, no
  allocation); trace-off is a predictable null-pointer branch.

## Reproduction

```bash
./scripts/build linux-clang-development
./out/build/linux-clang-development/modules/input/ludus_input_bench
```

The `ludus_input_alloc_tests` zero-allocation gate runs under:

```bash
./scripts/test linux-clang-debug -L alloc
```
