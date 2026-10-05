# ADR 0022: Bounded parsing foundation and JSON extraction

Status: Accepted for the P1 implementation.

## Context

Content embeds the pinned yyjson codec, pool ownership, structural validation,
and a canonical writer. Audio consumes that facade. Other future consumers need
bounded reading and diagnostics without acquiring Content's resource concepts.
The old JSON reader exposes a root after structural rejection and leaks an old
pool when retrying a failed parse.

## Decision

Export a small allocation-free `FoundationParsing` target and an optional
`FoundationParsingJson` target that privately links the existing yyjson 0.10.0
library. Keep syntax recognition separate from Content/Audio typed schemas.
Preserve the Content facade, document acceptance limits, and numeric conversion
behavior through a thin adapter. FoundationBase's emergency and diagnostic
control codec remains independent.

Use the existing AllocationDomain for one fallible owned pool, immutable input,
explicit admission limits, reusable retained storage, and no usable partial
root. Bound recursion and duplicate comparison with hard ceilings. Reject deep
containers before allocation; validate remaining structural limits after the
bounded DOM is built. Record unknown DOM source offsets honestly. Add native,
SDK, and browser contract coverage using the repository's pinned toolchains.

## Consequences

Content/Audio schemas and canonical valid output do not change. Failed reads
can be retried safely. Writer strings now reject invalid UTF-8; Finish appends
one newline once, and writes after Finish fail. The original Content node type and opaque
member are retained for source compatibility, including forward declarations
and unambiguous member pointers. This is not a stable binary ABI promise.

A whole document still requires the codec's conservative pool estimate. Value,
array, member, and string limits do not prevent construction of an otherwise
admitted DOM. No SIMD backend switch, source snapshot, generic lexer/AST,
streaming mode, editor recovery, or cooker is justified by this extraction.
Those remain measured consumer-driven phases in the
[architecture and literature review](../architecture/parsing.md).
