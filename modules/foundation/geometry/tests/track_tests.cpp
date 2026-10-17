// Scalar tracks against the G0 corpus: Step/Linear/Hermite spans, span sides,
// every boundary policy, Hermite overshoot, holds and duplicate key times.

#include "geometry_test_support.hpp"

#include <ludus/foundation/geometry/track.hpp>
#include <ludus/foundation/math/scalar.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <limits>

using namespace ludus::foundation::geometry;
using ludus::foundation::BorrowedView;
using ludus::foundation::core::StaticArray;
namespace fixtures = ludus::geometry_fixtures;
namespace math = ludus::foundation::math;

namespace
{
constexpr usize HOLD_KEY_COUNT = 3;
// Rounded-value regression envelope; values are float32 results of float64 arithmetic (not a bound).
constexpr float64 VALUE_TOLERANCE = 1e-6;

[[nodiscard]] TrackKey ToKey(const fixtures::TrackKey& key) noexcept
{
    TrackKey result;
    result.Time = key.Time;
    result.Value = key.Value;
    result.InSlope = key.InSlope;
    result.OutSlope = key.OutSlope;
    result.Interpolation = static_cast<TrackInterpolation>(static_cast<uint8>(key.Interpolation));
    return result;
}

[[nodiscard]] BoundaryPolicy ToPolicy(fixtures::BoundaryPolicy policy) noexcept
{
    switch (policy)
    {
        case fixtures::BoundaryPolicy::Reject:
            return BoundaryPolicy::Reject;
        case fixtures::BoundaryPolicy::Clamp:
            return BoundaryPolicy::Clamp;
        case fixtures::BoundaryPolicy::Hold:
            return BoundaryPolicy::Hold;
        case fixtures::BoundaryPolicy::Loop:
            return BoundaryPolicy::Loop;
        case fixtures::BoundaryPolicy::PingPong:
            return BoundaryPolicy::PingPong;
    }
    return BoundaryPolicy::Reject;
}

[[nodiscard]] GeometryStatus ToStatus(fixtures::ExpectedStatus status) noexcept
{
    switch (status)
    {
        case fixtures::ExpectedStatus::Success:
            return GeometryStatus::Success;
        case fixtures::ExpectedStatus::InvalidDomain:
            return GeometryStatus::InvalidDomain;
        case fixtures::ExpectedStatus::Degenerate:
            return GeometryStatus::Degenerate;
        case fixtures::ExpectedStatus::IllConditioned:
            return GeometryStatus::IllConditioned;
    }
    return GeometryStatus::InvalidArgument;
}

[[nodiscard]] bool IsClose(float32 actual, float64 expected) noexcept
{
    return math::Abs(static_cast<float64>(actual) - expected) <= VALUE_TOLERANCE * math::Max(1.0, math::Abs(expected));
}

template <usize Count>
[[nodiscard]] GeometryStatus PrepareKeys(const BorrowedView<const fixtures::TrackKey>& source,
                                         StaticArray<TrackKey, Count>& outKeys,
                                         PreparedTrack& outTrack,
                                         GeometryDiagnostic* outDiagnosticOrNull) noexcept
{
    for (usize i = 0; i < source.GetSize(); ++i)
    {
        outKeys[i] = ToKey(source[i]);
    }
    return TryPrepareTrack(BorrowedView<const TrackKey>(outKeys.GetData(), source.GetSize()),
                           test_support::MakeLimits(),
                           outTrack,
                           outDiagnosticOrNull);
}
} // namespace

TEST_CASE("Mixed track matches every corpus sample, side and boundary policy", "[geometry][track][fixtures]")
{
    const fixtures::TrackFixture& fixture = fixtures::TRACK_FIXTURES[0];
    REQUIRE(std::strcmp(fixture.Name, "mixed_track") == 0);
    StaticArray<TrackKey, fixtures::mixed_track::KEY_COUNT> keys;
    PreparedTrack track;
    REQUIRE(PrepareKeys(fixture.Keys, keys, track, nullptr) == GeometryStatus::Success);
    const TrackView view = track.GetView();
    REQUIRE(view.StartTime == 0.0);
    REQUIRE(view.EndTime == 3.5);

    for (usize i = 0; i < fixture.Samples.GetSize(); ++i)
    {
        const fixtures::TrackSampleReference& reference = fixture.Samples[i];
        INFO("time " << reference.Time << " sample " << i);
        const SpanSide side = reference.Side == fixtures::SpanSide::Incoming ? SpanSide::Incoming : SpanSide::Outgoing;
        const TrackSample sentinel{ .Value = 42.0F, .DerivativePerSecond = -42.0F };
        TrackSample sample = sentinel;
        const GeometryStatus status =
            TryEvaluateTrack(view, TrackTime{ .Seconds = reference.Time }, ToPolicy(reference.Policy), side, sample);
        REQUIRE(status == ToStatus(reference.Status));
        if (status == GeometryStatus::Success)
        {
            REQUIRE(IsClose(sample.Value, reference.Value) == true);
            REQUIRE(IsClose(sample.DerivativePerSecond, reference.DerivativePerSecond) == true);
        }
        else
        {
            REQUIRE((sample.Value == sentinel.Value && sample.DerivativePerSecond == sentinel.DerivativePerSecond));
        }
    }

    // Stored key values are Exact at keys.
    TrackSample sample;
    REQUIRE(TryEvaluateTrack(view, TrackTime{ .Seconds = 1.0 }, BoundaryPolicy::Reject, SpanSide::Outgoing, sample) ==
            GeometryStatus::Success);
    REQUIRE(sample.Value == 1.0F);
    // Hermite overshoot is authored behavior; evaluation never clamps it.
    REQUIRE(fixture.HasOvershoot == true);
    REQUIRE(TryEvaluateTrack(view,
                             TrackTime{ .Seconds = fixture.OvershootTime },
                             BoundaryPolicy::Reject,
                             SpanSide::Outgoing,
                             sample) == GeometryStatus::Success);
    REQUIRE(IsClose(sample.Value, fixture.OvershootValue) == true);
    REQUIRE(sample.Value > 1.0F);
    REQUIRE(math::Abs(sample.DerivativePerSecond) <= 1e-6F);
}

TEST_CASE("Track policies and arguments outside the frozen vocabulary are rejected", "[geometry][track]")
{
    const fixtures::TrackFixture& fixture = fixtures::TRACK_FIXTURES[0];
    StaticArray<TrackKey, fixtures::mixed_track::KEY_COUNT> keys;
    PreparedTrack track;
    REQUIRE(PrepareKeys(fixture.Keys, keys, track, nullptr) == GeometryStatus::Success);
    TrackSample sample;
    const TrackTime inside{ .Seconds = 0.5 };
    REQUIRE(TryEvaluateTrack(track.GetView(), inside, BoundaryPolicy::Wrap, SpanSide::Outgoing, sample) ==
            GeometryStatus::InvalidArgument);
    REQUIRE(TryEvaluateTrack(track.GetView(), inside, static_cast<BoundaryPolicy>(99), SpanSide::Outgoing, sample) ==
            GeometryStatus::InvalidArgument);
    REQUIRE(TryEvaluateTrack(track.GetView(), inside, BoundaryPolicy::Reject, static_cast<SpanSide>(7), sample) ==
            GeometryStatus::InvalidArgument);
    REQUIRE(TryEvaluateTrack(track.GetView(),
                             TrackTime{ .Seconds = std::numeric_limits<float64>::quiet_NaN() },
                             BoundaryPolicy::Clamp,
                             SpanSide::Outgoing,
                             sample) == GeometryStatus::NonFiniteInput);
    REQUIRE(TryEvaluateTrack(TrackView{}, inside, BoundaryPolicy::Reject, SpanSide::Outgoing, sample) ==
            GeometryStatus::MissingCache);
    // Inside the domain every policy evaluates the same value.
    for (const BoundaryPolicy policy : {BoundaryPolicy::Reject,
                                        BoundaryPolicy::Clamp,
                                        BoundaryPolicy::Hold,
                                        BoundaryPolicy::Loop,
                                        BoundaryPolicy::PingPong})
    {
        TrackSample policySample;
        REQUIRE(
            TryEvaluateTrack(track.GetView(), TrackTime{ .Seconds = 2.0 }, policy, SpanSide::Outgoing, policySample) ==
            GeometryStatus::Success);
        REQUIRE(IsClose(policySample.Value, 1.5) == true);
        REQUIRE(IsClose(policySample.DerivativePerSecond, -0.5) == true);
    }
}

TEST_CASE("Duplicate key times are rejected and the active track is preserved", "[geometry][track][fixtures]")
{
    StaticArray<TrackKey, fixtures::mixed_track::KEY_COUNT> keys;
    PreparedTrack track;
    REQUIRE(PrepareKeys(fixtures::TRACK_FIXTURES[0].Keys, keys, track, nullptr) == GeometryStatus::Success);

    const fixtures::TrackFixture& duplicate = fixtures::TRACK_FIXTURES[1];
    REQUIRE(std::strcmp(duplicate.Name, "duplicate_time") == 0);
    StaticArray<TrackKey, fixtures::duplicate_time::KEY_COUNT> duplicateKeys;
    GeometryDiagnostic diagnostic;
    REQUIRE(PrepareKeys(duplicate.Keys, duplicateKeys, track, &diagnostic) == ToStatus(duplicate.Status));
    REQUIRE(diagnostic.Code == GeometryStatus::InvalidDomain);
    REQUIRE(diagnostic.SourceElement == 2);
    REQUIRE(std::strcmp(diagnostic.Property, "Keys.Time") == 0);
    REQUIRE(diagnostic.Domain == DomainKind::TrackTime);
    REQUIRE(track.GetView().Spans.GetSize() == fixtures::mixed_track::KEY_COUNT - 1);

    duplicateKeys[2].Time = 0.5;
    REQUIRE(TryPrepareTrack(BorrowedView<const TrackKey>(duplicateKeys.GetData(), duplicateKeys.GetSize()),
                            test_support::MakeLimits(),
                            track,
                            nullptr) == GeometryStatus::InvalidDomain);
    duplicateKeys[2].Time = 2.0;
    duplicateKeys[1].OutSlope = std::numeric_limits<float32>::infinity();
    REQUIRE(TryPrepareTrack(BorrowedView<const TrackKey>(duplicateKeys.GetData(), duplicateKeys.GetSize()),
                            test_support::MakeLimits(),
                            track,
                            nullptr) == GeometryStatus::NonFiniteInput);
    duplicateKeys[1].OutSlope = 0.0F;
    duplicateKeys[1].Interpolation = static_cast<TrackInterpolation>(9);
    REQUIRE(TryPrepareTrack(BorrowedView<const TrackKey>(duplicateKeys.GetData(), duplicateKeys.GetSize()),
                            test_support::MakeLimits(),
                            track,
                            nullptr) == GeometryStatus::InvalidArgument);
    REQUIRE(TryPrepareTrack(BorrowedView<const TrackKey>(duplicateKeys.GetData(), 1),
                            test_support::MakeLimits(),
                            track,
                            nullptr) == GeometryStatus::InvalidArgument);
    WorkLimits limits = test_support::MakeLimits();
    limits.MaxSpans = 2;
    duplicateKeys[1].Interpolation = TrackInterpolation::Linear;
    REQUIRE(TryPrepareTrack(BorrowedView<const TrackKey>(duplicateKeys.GetData(), duplicateKeys.GetSize()),
                            limits,
                            track,
                            nullptr) == GeometryStatus::BudgetExceeded);
    REQUIRE(track.GetView().Spans.GetSize() == fixtures::mixed_track::KEY_COUNT - 1);
}

TEST_CASE("Timed holds are explicit stationary intervals", "[geometry][track]")
{
    // A hold is authored as equal values over a positive interval, never as equal key times.
    const StaticArray<TrackKey, HOLD_KEY_COUNT> keys{{
        TrackKey{ .Time = 0.0, .Value = 2.0F, .Interpolation = TrackInterpolation::Linear },
        TrackKey{ .Time = 1.0, .Value = 2.0F, .Interpolation = TrackInterpolation::Linear },
        TrackKey{ .Time = 4.0, .Value = 5.0F, .Interpolation = TrackInterpolation::Linear },
    }};
    PreparedTrack track;
    REQUIRE(TryPrepareTrack(BorrowedView<const TrackKey>(keys.GetData(), keys.GetSize()),
                            test_support::MakeLimits(),
                            track,
                            nullptr) == GeometryStatus::Success);
    TrackSample sample;
    REQUIRE(TryEvaluateTrack(track.GetView(),
                             TrackTime{ .Seconds = 0.5 },
                             BoundaryPolicy::Reject,
                             SpanSide::Outgoing,
                             sample) == GeometryStatus::Success);
    REQUIRE((sample.Value == 2.0F && sample.DerivativePerSecond == 0.0F));
    // The outgoing side at the end of the hold already ramps; the incoming side is still held.
    REQUIRE(TryEvaluateTrack(track.GetView(),
                             TrackTime{ .Seconds = 1.0 },
                             BoundaryPolicy::Reject,
                             SpanSide::Outgoing,
                             sample) == GeometryStatus::Success);
    REQUIRE((sample.Value == 2.0F && sample.DerivativePerSecond == 1.0F));
    REQUIRE(TryEvaluateTrack(track.GetView(),
                             TrackTime{ .Seconds = 1.0 },
                             BoundaryPolicy::Reject,
                             SpanSide::Incoming,
                             sample) == GeometryStatus::Success);
    REQUIRE((sample.Value == 2.0F && sample.DerivativePerSecond == 0.0F));
    // Hold outside the domain keeps the endpoint value with zero derivative; Clamp keeps the one-sided slope.
    REQUIRE(TryEvaluateTrack(track.GetView(),
                             TrackTime{ .Seconds = 9.0 },
                             BoundaryPolicy::Hold,
                             SpanSide::Outgoing,
                             sample) == GeometryStatus::Success);
    REQUIRE((sample.Value == 5.0F && sample.DerivativePerSecond == 0.0F));
    REQUIRE(TryEvaluateTrack(track.GetView(),
                             TrackTime{ .Seconds = 9.0 },
                             BoundaryPolicy::Clamp,
                             SpanSide::Outgoing,
                             sample) == GeometryStatus::Success);
    REQUIRE((sample.Value == 5.0F && sample.DerivativePerSecond == 1.0F));
}
