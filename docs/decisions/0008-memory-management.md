# ADR 0008: Explicit Memory Ownership with Measured Backend Selection

## Status

Proposed, revised after [independent adversarial review](../architecture/memory-management-review.md). Architecture only; no memory system implementation or backend change is included. Number 0007 belongs to the accepted foundational-header decision.

## Context

The original audit at `07cf666` and independent review at `d4a84ca` find custom `Array`/`StaticArray` containers, bounded logging and CPU tracing, and a Vulkan RHI without device submission. `Array` already has an aligned, fallible allocation seam. The engine has no measured need for a handwritten general-purpose allocator, production global interception, or a collection of specialized heaps. Concurrent RHI/build work is noted separately in the design's audit scope.

The [memory-management design](../architecture/memory-management.md) audits the current code and literature, specifies ownership/failure/lifetime contracts, and defines performance gates and independently reviewable implementation phases. Its M1–M12 records contain selected/rejected alternatives, evidence, trade-offs and revisit conditions.

## Decision

Introduce a small `FoundationMemory` module above Base and below Containers. Start with the system allocator. Evaluate a pinned mimalloc backend through explicit APIs and representative benchmarks before changing a platform default. Do not implement a Ludus malloc replacement or override global allocation functions in the engine/SDK.

Bind original backend identity and subsystem attribution through stable process-lifetime allocation domains. Keep `Array` heap-backed; initially redirect its existing seam, then add one domain pointer with explicit copy/move/swap rules. Preserve fixed inline containers. Use reserve/reuse first and add one explicit bounded linear arena/scratch scope only for a measured consumer. Future frame storage requires CPU completion and, where applicable, GPU retirement before reuse.

Maintain measured-cost requested-byte domain totals with explicit validity; these are not physical backend peaks. Make per-allocation validation and capture optional. Memory owns allocation accounting; the existing profiler owns capture and external presentation. Start with gauges, then caller-quiesced bounded activity sessions. Complete live-set claims require a proven domain ownership baseline, not merely startup-time activation. Defer live attachment, stacks and sharded tracking until needed. Preserve C++23 storage-lifetime semantics, sanitizer support, native allocation/destruction boundaries, and the allocation-free Base emergency path.

## Consequences

The engine gains a replaceable, observable allocation boundary without committing to a new heap algorithm. A later 24-to-32-byte `Array` layout change requires density measurements, a full SDK rebuild and a versioned allocation-seam link check. Domain counter cost remains subject to measurement, and unmigrated/foreign allocations remain explicit coverage gaps. The process backend remains alive through static/TLS teardown. Detailed tracking cannot claim complete histories without a proven baseline and loss-free coverage. Memory remains above the `core.h` include boundary.

On acceptance, this ADR refines [profiling design §7](../architecture/profiling-final.md#7-step-6--memory-profiling-ownership-c4): allocation provenance can be retained by the owner/domain instead of a mandatory release prefix; rich origin/identity metadata is optional; aggregate peaks and process coverage must be labeled accurately. A future implementation amendment to ADR 0003 will permit private C-runtime allocation calls within Memory's backend only. It does not relax the exception, dependency, primitive-type, or header-budget rules.

The audit identified existing container, unique-ownership and trace-alignment/lifecycle prerequisites; these remain separate code fixes. Start with Phase 0A: repair and test aliased fill in `Array::Resize`/`TryResize` across growth. Other repairs gate only their dependent migrations; they are not a prerequisite for every byte-API experiment. Seven requested literature chapters were available locally, Hill was recovered externally, and Hultquist's text remains unavailable. The independent review did not repeat that literature study. No decision assumes the missing text's contents. Backend or specialized-allocator changes require new measured evidence under the design's revisit criteria.
