#pragma once

// MathStatus — the single lightweight status code returned by every fallible
// FoundationMath operation. There are no strings, no owning "expected" wrapper
// and no allocation: a checked API returns MathStatus by value and writes its
// caller-owned output(s) only when the status is Success (see ADR 0010 §4).
//
// A normal geometric *miss* is NOT a failure: queries return Success and carry a
// Miss/Hit (or equivalent) classification in their result record. A failing
// status always means the inputs were invalid or numerically unsupported, and in
// that case every output is left unchanged.
//
// Multi-failure precedence is not an ABI promise: when several preconditions are
// violated at once, which failing code is returned may change between versions,
// so tests should assert "a relevant failure", not a specific ordering.

#include <ludus/foundation/base/types.h>

namespace ludus::foundation::math
{
using core::uint8;

enum class MathStatus : uint8
{
    // The operation succeeded; outputs are written and meaningful.
    Success = 0,

    // An input was NaN or infinite where a finite value is required.
    NonFiniteInput,

    // An input violated a documented domain precondition that is not purely a
    // finiteness problem (e.g. a non-positive length threshold, reversed
    // interval bounds, a zero-length direction, a bound of zero).
    InvalidArgument,

    // The input is mathematically degenerate for this operation (e.g. a
    // zero-length vector to normalize, coincident eye/target, a basis that is
    // not a rotation, a singular linear block).
    Degenerate,

    // The operation is numerically ill-conditioned beyond the policy threshold
    // (e.g. an inverse whose reciprocal condition estimate or residual fails).
    IllConditioned,

    // A finite, well-formed input produced a result that is not representable in
    // the output type (overflow / narrowing to non-finite).
    OutOfRange,

    // Two spans that must match in length did not (batch APIs).
    SizeMismatch,
};

// IsSuccess — tiny readable predicate for call sites that branch on status.
[[nodiscard]] constexpr bool IsSuccess(MathStatus status) noexcept
{
    return status == MathStatus::Success;
}
} // namespace ludus::foundation::math
