---
description: Choose and reproduce deterministic PCG32 streams and addressed Philox samples.
---

# Use deterministic randomness

Use `Ludus::FoundationMath` to make a gameplay choice repeatable or regenerate
procedural results independently of worker scheduling. Link that target and
include the RNG header explicitly; randomness is not included by Base's `core.h`.
These APIs are allocation-free, exception-free and `noexcept`.

## Choose the source

| Your task | Use | What you own |
| --- | --- | --- |
| Ordered choices in one subsystem | `RandomStream` in `random.hpp` (PCG32 XSH-RR v1) | Stream lifetime, seed, selector and logical call order |
| Independent events or procedural chunks processed in any order | Functions in `addressed_random.hpp` (Philox4x32-10) | Root seed, persistent domain IDs and logical addresses |
| Security tokens or unpredictable secret values | A cryptographic facility outside these APIs | Neither generator provides a security guarantee |

There is no global RNG service. A default `RandomStream` uses deterministic
seed/selector zero; it does not gather entropy. Root seed zero is also valid for
addressed samples. Select and record seeds explicitly in your game.

## Sample one logical event

This complete function returns a ticket in `[0,1000)` for one drop. Domain `7`
and dimension `0` are illustrative assignments: give them persistent meanings
in your own content protocol. A failed call leaves `ticket` unchanged.

```cpp
#include <ludus/foundation/math/addressed_random.hpp>

// Thanks to Salmon, Moraes, Dror and Shaw, "Parallel Random Numbers: As Easy
// as 1, 2, 3", SC11 (2011), section 4.3, for counter-based random access:
// https://www.thesalmons.org/john/random123/papers/random123sc11.pdf
// Ludus defines the domain and scope/event/dimension contracts used here.
[[nodiscard]] ludus::foundation::math::MathStatus SampleDropTicket(
    ludus::foundation::core::uint64 worldSeed,
    ludus::foundation::core::uint64 entityId,
    ludus::foundation::core::uint64 dropOccurrence,
    ludus::foundation::core::uint32& ticket) noexcept
{
    using namespace ludus::foundation::math;
    RandomKey key;
    const MathStatus keyStatus = TryMakeRandomKey(worldSeed, 7, key);
    if (keyStatus != MathStatus::Success)
    {
        return keyStatus;
    }

    RandomAddress address;
    const MathStatus addressStatus = TryMakeRandomAddress(
        { .Scope = entityId, .Event = dropOccurrence, .Dimension = 0 }, address);
    if (addressStatus != MathStatus::Success)
    {
        return addressStatus;
    }
    return TrySampleBounded(key, address, 1000, ticket);
}
```

Calling this function twice with the same inputs produces the same ticket.
Reordering jobs does not change the ticket because no shared stream advances.
To represent another drop, change its **logical occurrence**, not its worker
index. Save occurrence counters with their owner when restoring a world.

| Input | Contract |
| --- | --- |
| Root seed | Any `uint64`; record the selected value |
| Domain | A persistent, nonzero `uint32` ID such as loot or presentation |
| Scope | A stable `uint64` entity/chunk identity |
| Event | An explicit occurrence, `0` through `4294967295` |
| Dimension | A stable semantic slot, `0` through `65535` |

Use `TryMakeRandomAddress` on wide external values **before narrowing** them.
It returns `OutOfRange` for an oversized event/dimension. Do not use pointers,
thread IDs, job indices, container iteration positions or shared draw counters
as logical identities. Detect counter exhaustion before incrementing; wrapping
an event reuses an earlier address.

Different nonzero domain IDs produce distinct keys within one root seed. This
does not promise statistical independence or collision freedom across different
root seeds. Random keys and sampled values are not unique object IDs.

## Select the output and handle failure

- `SampleUInt32(key, address)` returns raw bits.
- `SampleFloat01(key, address)` uses the high 24 bits and returns `[0,1)`.
- `TrySampleBounded(key, address, bound, out)` returns an unbiased integer in
  `[0,bound)`. Use it instead of reducing raw bits with `% bound`.
- For many samples with the same bound, build a `PreparedBound32` with
  `TryPrepareBound32` once and pass it to the bounded overload. This amortizes
  threshold preparation; it does not change the result.
- `TrySampleBlock` returns raw values for four consecutive dimensions in one
  evaluation. Its starting dimension must be a multiple of four, at most
  `65532`. Each lane matches `SampleUInt32` at the corresponding dimension.

All checked addressed functions preserve their outputs on failure. Domain zero,
bound zero and an unaligned block request return `InvalidArgument`. Bounded
sampling keeps rejection attempts within the same logical address; after
`65536` unsuccessful attempts it returns `OutOfRange`. Report failure to the
owner rather than silently substituting a biased result. Bound one succeeds
with zero. Changing a bound changes the mapping and can change the result.

Raw, float and bounded calls at the same address share underlying randomness;
assign different semantic dimensions when you need separate choices. A raw
block is not four already bounded tickets.

## Save and resume an ordered stream

PCG is useful when one owner defines the sequence of choices. This function
illustrates checked seeding, a checkpoint, and exact continuation:

```cpp
#include <ludus/foundation/math/random.hpp>

// Thanks to Melissa O'Neill, "pcg-c-basic", for the PCG32 reference API:
// https://www.pcg-random.org/using-pcg-c-basic.html
[[nodiscard]] bool VerifyStreamContinuation() noexcept
{
    using namespace ludus::foundation::math;
    RandomStream stream;
    if (!stream.TryReseed(42, 54))
    {
        return false;
    }

    ludus::foundation::core::uint32 ticket = 0;
    if (stream.TryNextBounded(100, ticket) != MathStatus::Success)
    {
        return false;
    }
    const RandomState checkpoint = stream.GetState();
    const ludus::foundation::core::uint32 expectedNext = stream.NextUInt32();

    RandomStream resumed;
    if (resumed.TryRestore(checkpoint) != MathStatus::Success)
    {
        return false;
    }
    return resumed.NextUInt32() == expectedNext;
}
```

Selectors must be at most `0x7fffffffffffffff`; an invalid selector leaves the
stream unchanged. `TryNextBounded(0, out)` returns `InvalidArgument`, preserving
both stream and output. Its rejection loop has no hard worst-case iteration
bound. Each successful raw/float draw advances the stream; bounded draws can
consume more than one raw draw. Changing call order or bounds can therefore
change later choices.

`RandomState` contains `Version`, `State` and `Increment`. `TryRestore` rejects
an unsupported version or an even increment without modifying the stream.
A checkpoint resumes RNG state, not the whole game. Disk persistence must encode
fields explicitly with fixed widths and a specified byte order; never dump the
struct's memory/padding. A public PCG wire codec is not implemented yet.

## Record enough to reproduce a result

For addressed samples, record the root seed (or stored derived key), domain and
its semantic assignment, scope/event/dimension, generator identity
`Philox4x32-10`, and these shipped constants:

- `RandomKeyDerivationVersion`
- `RandomAddressLayoutVersion`
- `RandomBoundedMappingVersion` when using bounded samples

Also record the sampler and its parameters, such as the bound, plus your game
build/content version and address-assignment version. For ordered streams,
record the PCG algorithm/state version and checkpoints, or the original
seed/selector with the exact logical draw sequence. Retain the matching
contracts when replaying old data; reseeding a changed algorithm is not a
compatibility strategy.

When debugging a mismatch, compare the recipe first, then raw bits, then the
mapped result, then downstream gameplay. A seed alone cannot reproduce changed
content, input order, occurrence counters or floating-point simulation behavior.
There is no automatic world replay manifest or replay loader in these primitives.

Give gameplay, presentation and previews separate randomness ownership.
A preview should sample independent logical addresses or a **copy** of a stream;
it must not advance the live gameplay stream. RNG tracing and runtime inspection
are not currently editor features.

## Sources and further reading

The [delivery plan](../../architecture/randomness.md#delivery-plan-and-alternatives)
tracks remaining implementation milestones and the research reading backlog to
evaluate after the initial architecture is implemented.

Thanks to the authors below for the ideas used in the implementation. The
[architecture](../../architecture/randomness.md)
and [consulted chapter review](../../architecture/randomness-gems-review.md)
explain adaptations, compatibility decisions and remaining proposals.

- **John K. Salmon, Mark A. Moraes, Ron O. Dror and David E. Shaw**, *Parallel
  Random Numbers: As Easy as 1, 2, 3*, SC11 (2011), §4.3:
  [Philox and counter-based generation](https://www.thesalmons.org/john/random123/papers/random123sc11.pdf).
- **Melissa O'Neill**, *pcg-c-basic*:
  [PCG32 reference usage](https://www.pcg-random.org/using-pcg-c-basic.html).
- **Daniel Lemire**, *Fast Random Integer Generation in an Interval*, ACM
  TOMACS 29(1), 2019, Algorithm 5:
  [multiply-high rejection mapping](https://arxiv.org/abs/1805.10941).
- **Sebastiano Vigna**, *splitmix64.c* (2015):
  [finalizer used in key derivation](https://prng.di.unimi.it/splitmix64.c).
  Ludus adapts the finalizer, not the stateful SplitMix generator.
- **Guy W. Lecky-Thompson**, *Predictable Random Numbers*, Game Programming
  Gems 1, §2.0, pp. 133–140: independent procedural regeneration.
- **Bruce Dawson**, *Game Input Recording and Playback*, Game Programming
  Gems 2, §1.16, pp. 105–111: simulation/presentation isolation.
- **James McNeill**, *Fast Base-2 Functions for Logarithms and Random Number
  Generation*, Game Programming Gems 3, §2.1, pp. 157–159: rejection sampling;
  Ludus uses Lemire's mapping instead of the chapter's bit-mask mapping.

The chapter review contains bibliographic details; its local PDF paths refer to
an ignored reference library, not public wiki downloads. No chapter code is
copied into these examples. See the
[module guide](../../modules/foundation/math.md)
and [public addressed API](https://github.com/Alegruz/Ludus/blob/main/modules/foundation/math/include/ludus/foundation/math/addressed_random.hpp)
for the implementation contract. Weighted choice, shuffle policies, entropy
acquisition and GPU sampling remain separate future work.
