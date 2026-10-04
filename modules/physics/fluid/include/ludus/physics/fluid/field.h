#pragma once

#include <ludus/foundation/base/core.h>

#include <span>

namespace ludus::physics::fluid
{
using foundation::float64;
using foundation::uint8;
using foundation::usize;

struct Point final
{
    float64 X = 0.0;
    float64 Y = 0.0;
};

enum class Status : uint8
{
    Success,
    InvalidArgument,
    NotReady,
    AllocationFailure,
    OutsideDomain,
    SolidCell,
    WorkLimit,
};

// Centered rectangular basin in meters. Resolution is 4..256 per axis;
// half extents are 0.01..1000 m. Constant bed depth; no wet/dry shoreline.
struct Config final
{
    usize Width = 48;
    usize Height = 64;
    Point HalfExtent{30.0, 40.0};
    float64 Depth = 4.0;
    float64 Gravity = 9.8;
    float64 MaxSpeed = 10.0;
    float64 MaxPressure = 1000.0;
    float64 MinimumDepth = 0.001;
    float64 MomentumDamping = 0.06;
    float64 FoamDecay = 0.65;
    float64 MaterialDecay = 0.035;
    usize MaxSubsteps = 256;
};

struct CellState final
{
    float64 Height = 0.0; // Surface elevation relative to the resting surface.
    float64 Foam = 0.0;
    float64 DisplacementX = 0.0;
    float64 DisplacementY = 0.0;
};

struct Sample final
{
    float64 VelocityX = 0.0;
    float64 VelocityY = 0.0;
    float64 Height = 0.0;
    float64 Foam = 0.0;
    float64 DisplacementX = 0.0;
    float64 DisplacementY = 0.0;
    float64 Curl = 0.0;
    float64 Divergence = 0.0;
};

struct Diagnostics final
{
    float64 Energy = 0.0; // Reference energy diagnostic, not an exact invariant.
    float64 MaxCurl = 0.0;
    float64 MaxDivergence = 0.0;
    float64 MaxHeight = 0.0;
    float64 IntegratedHeight = 0.0;
    float64 MinDepth = 0.0;
};

struct Stroke final
{
    Point Start;
    Point End;
    float64 Radius = 4.0;
    float64 Spacing = 0.4;
    float64 Strength = 1.8; // Velocity change per meter of stroke.
};

struct StepInfo final
{
    usize Substeps = 0;
    float64 AdvancedSeconds = 0.0;
};

// Single-owner CPU shallow-water field. All storage is private. Initialization
// allocates; equal-resolution reinitialization, reset, forcing, stepping and
// sampling reuse it. No renderer, callbacks, game types or global state.
class Field final
{
public:
    Field() noexcept = default;
    ~Field() noexcept;
    Field(const Field&) = delete;
    Field& operator=(const Field&) = delete;
    Field(Field&& other) noexcept;
    Field& operator=(Field&& other) noexcept;

    // Invalid configuration or failed allocation preserves the old field.
    [[nodiscard]] Status TryInitialize(const Config& config) noexcept;
    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] Config GetConfig() const noexcept;

    // Empty views mean all-fluid / zero initial state. Nonempty views must be
    // Width*Height, row-major. Values and total depth are validated before reset.
    // Solid cells have zero state; initial foam is 0..1, displacement is +/-12 m.
    [[nodiscard]] Status TryReset(std::span<const uint8> solids = {}, std::span<const CellState> initial = {}) noexcept;
    // MAC face arrays: (Width+1)*Height U and Width*(Height+1) V.
    // Closed faces are forced to zero. Invalid inputs preserve state.
    [[nodiscard]] Status TrySetVelocity(std::span<const float64> u, std::span<const float64> v) noexcept;
    // Surface pressure divided by density, in m^2/s^2. Held until replaced.
    // Empty clears it; otherwise Width*Height values, bounded by MaxPressure.
    [[nodiscard]] Status TrySetPressure(std::span<const float64> pressure) noexcept;
    // Compact momentum brush integrated along a segment. Rejects segments that
    // cross solid cells. Work is bounded; rejection applies no partial forcing.
    [[nodiscard]] Status TryAddStroke(const Stroke& stroke) noexcept;

    // Conservative step bound for the CURRENT state. Call again after each substep
    // when updating external sources; pressure is already held by the field.
    [[nodiscard]] Status TryGetStepLimit(float64& seconds) const noexcept;
    // Accepts 0..0.25 seconds, with adaptive substeps. Invalid input preserves
    // state and output. WorkLimit reports explicit partial progress in info.
    [[nodiscard]] Status TryAdvance(float64 seconds, StepInfo& info) noexcept;
    // Success writes output; invalid/outside/solid queries preserve it.
    [[nodiscard]] Status TrySample(Point world, Sample& output) const noexcept;
    [[nodiscard]] bool IsFluid(usize x, usize y) const noexcept;
    // Invalid/solid indices return zero; no borrowed storage escapes the field.
    [[nodiscard]] CellState GetCell(usize x, usize y) const noexcept;
    [[nodiscard]] Diagnostics GetDiagnostics() const noexcept;

private:
    struct Storage;
    Storage* mStorage = nullptr;
};
} // namespace ludus::physics::fluid
