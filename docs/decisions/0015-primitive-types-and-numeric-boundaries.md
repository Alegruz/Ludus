# ADR 0015: Native primitive types and checked numeric boundaries

## Status

Accepted for implementation in this change.

## Context

The existing FoundationBase aliases are native and ABI-compatible but do not
verify all storage assumptions or provide a shared safe numeric boundary API.
Protocol queue size arithmetic can wrap before its limit check. Native and
wasm32 size widths differ, while wire formats need stable fixed-width fields.

## Decision

Preserve every existing alias and namespace. Cheap storage checks remain in
`types.h`; a single private `types.cpp` verifies 8-bit bytes, signed/unsigned
ranges, native size/difference compatibility and IEEE binary32/64 using
`<climits>`, `<limits>` and `<type_traits>`. This is a narrow extension of
ADR 0003. It adds no runtime initialization and no universal header dependency.

Add explicit opt-in `checked_integer.hpp` and `byte_order.hpp` headers in
FoundationBase. Checked integer cast uses standard `std::in_range`; arithmetic
uses the generic overflow builtins of validated Clang 18 and pinned Emscripten.
Inputs are the existing integer aliases; same-type arithmetic avoids accidental
mixed signedness/promotion policy. APIs are constexpr, noexcept and nodiscard;
failure leaves output unchanged, including output aliasing. No exceptions,
allocation, logging, assertion, saturation or undefined signed overflow occurs.

Endian codecs validate a byte span before unsigned integer reads/writes and
operate on bytes, preserving alignment and aliasing safety. Stable formats own
field widths, byte order, signed/float encoding and versioning. Native size,
pointer, bool, enum or struct object representations are not wire formats.

Keep ordinary native arithmetic on trusted hot paths. Domain-specific IDs,
units, SIMD, fixed point, half precision, float conversions, FP environment and
reflection belong to their owning modules. No primitive wrapper class or runtime
registry is added. ADR 0010 continues to own numerical algorithms and FP policy.

## Consequences

Existing scalar layouts and debugging remain unchanged. Numeric ingress and
allocation code can state failure explicitly and share safe implementation.
The LLVM builtin dependency is deliberate for today's supported toolchains;
a future non-Clang backend must supply equivalent defined arithmetic and pass
the same contract tests. Header isolation, byte vectors, exhaustive 8-bit
arithmetic, all cast pairs, limit values, native/wasm and installed SDK probes
validate the surface. See `docs/architecture/primitive-types.md` for the initial
design, actual Gems review, API contracts, examples and measurements.
