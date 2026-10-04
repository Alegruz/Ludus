#include <ludus/physics/fluid/field.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

using namespace ludus::physics::fluid;
using ludus::foundation::float64;
using ludus::foundation::usize;

TEST_CASE("Fluid initialization and invalid mutations preserve live state", "[fluid]")
{
    Field field;
    Sample sample;
    sample.Height = 123.0;
    REQUIRE(field.TrySample({}, sample) == Status::NotReady);
    REQUIRE(sample.Height == 123.0);
    REQUIRE(field.TryInitialize({}) == Status::Success);
    REQUIRE(field.TryAddStroke({{-10.0, 0.0}, {10.0, 0.0}}) == Status::Success);
    const auto before = field.GetDiagnostics();
    auto invalid = field.GetConfig();
    invalid.Width = static_cast<usize>(-1);
    CHECK(field.TryInitialize(invalid) == Status::InvalidArgument);
    invalid = field.GetConfig();
    invalid.Depth = std::numeric_limits<float64>::quiet_NaN();
    CHECK(field.TryInitialize(invalid) == Status::InvalidArgument);
    CHECK(field.GetDiagnostics().Energy == before.Energy);
    std::array<CellState, 1> shortState{};
    CHECK(field.TryReset({}, shortState) == Status::InvalidArgument);
    std::vector<float64> pressure(48 * 64);
    pressure.back() = std::numeric_limits<float64>::infinity();
    CHECK(field.TrySetPressure(pressure) == Status::InvalidArgument);
    CHECK(field.TryAddStroke({{0, 0}, {0, 0}}) == Status::InvalidArgument);
    CHECK(field.TryAddStroke({{-30, 0}, {0, 0}}) == Status::OutsideDomain);
    CHECK(field.GetDiagnostics().Energy == before.Energy);
    StepInfo info{77, 99.0};
    CHECK(field.TryAdvance(-1.0, info) == Status::InvalidArgument);
    CHECK(info.Substeps == 77);
    CHECK(info.AdvancedSeconds == 99.0);
    CHECK(field.TrySample({30.0, 0.0}, sample) == Status::OutsideDomain);
    CHECK(sample.Height == 123.0);
    Field moved(std::move(field));
    CHECK_FALSE(field.IsReady());
    CHECK(moved.GetDiagnostics().Energy == before.Energy);
    CHECK(moved.TryInitialize({}) == Status::Success);
    CHECK_FALSE(moved.IsActive());
    CHECK(moved.GetDiagnostics().Energy == 0.0);
}

TEST_CASE("Momentum strokes combine and resampling does not amplify forcing", "[fluid]")
{
    Field sparse, dense;
    REQUIRE(sparse.TryInitialize({}) == Status::Success);
    REQUIRE(dense.TryInitialize({}) == Status::Success);
    REQUIRE(sparse.TryAddStroke({{-10, 0}, {10, 0}}) == Status::Success);
    for (usize i = 0; i < 100; ++i)
    {
        const float64 x = -10.0 + static_cast<float64>(i) * 0.2;
        REQUIRE(dense.TryAddStroke({{x, 0}, {x + 0.2, 0}}) == Status::Success);
    }
    CHECK(std::abs(sparse.GetDiagnostics().Energy / dense.GetDiagnostics().Energy - 1.0) < 0.005);
    REQUIRE(sparse.TryAddStroke({{10, 0}, {-10, 0}}) == Status::Success);
    CHECK(sparse.GetDiagnostics().Energy < dense.GetDiagnostics().Energy * 1e-8);
    StepInfo info;
    REQUIRE(dense.TryAdvance(1.0 / 60.0, info) == Status::Success);
    CHECK(info.AdvancedSeconds == 1.0 / 60.0);
    CHECK(dense.GetDiagnostics().MaxHeight > 0.001);
    Sample sample;
    REQUIRE(dense.TrySample({}, sample) == Status::Success);
    CHECK(sample.VelocityX > 0.0);
    CHECK(sample.DisplacementX > 0.0);
}

TEST_CASE("Closed basin conserves integrated height without clipping under strong pressure", "[fluid]")
{
    Config config;
    config.Width = 16;
    config.Height = 12;
    config.HalfExtent = {4.0, 3.0};
    config.Depth = 0.1;
    Field field;
    REQUIRE(field.TryInitialize(config) == Status::Success);
    std::vector<CellState> initial(config.Width * config.Height);
    for (auto& cell : initial)
    {
        cell.Height = 0.05;
    }
    REQUIRE(field.TryReset({}, initial) == Status::Success);
    const auto mass = field.GetDiagnostics().IntegratedHeight;
    std::vector<float64> pressure(initial.size());
    pressure[6 * config.Width + 8] = 900.0;
    REQUIRE(field.TrySetPressure(pressure) == Status::Success);
    StepInfo info;
    for (usize i = 0; i < 180; ++i)
    {
        REQUIRE(field.TryAdvance(1.0 / 60.0, info) == Status::Success);
        CHECK(field.GetDiagnostics().MinDepth >= config.MinimumDepth - 1e-10);
    }
    CHECK(std::abs(field.GetDiagnostics().IntegratedHeight - mass) < 1e-9);
    CHECK(field.GetDiagnostics().MaxHeight > 0.05);
    REQUIRE(field.TrySetPressure({}) == Status::Success);
    const auto energy = field.GetDiagnostics().Energy;
    for (usize i = 0; i < 60; ++i)
    {
        REQUIRE(field.TryAdvance(1.0 / 60.0, info) == Status::Success);
    }
    CHECK(std::isfinite(field.GetDiagnostics().Energy));
    CHECK(energy > 0.0);
}

TEST_CASE("Solid masks isolate basins and reject a stroke before any partial forcing", "[fluid]")
{
    Config config;
    config.Width = 16;
    config.Height = 12;
    config.HalfExtent = {8, 6};
    Field field;
    REQUIRE(field.TryInitialize(config) == Status::Success);
    std::vector<ludus::foundation::uint8> solid(config.Width * config.Height);
    for (usize y = 0; y < config.Height; ++y)
    {
        solid[y * config.Width + 8] = 1;
    }
    REQUIRE(field.TryReset(solid) == Status::Success);
    CHECK(field.TryAddStroke({{-6, 0}, {6, 0}}) == Status::SolidCell);
    CHECK(field.TryAddStroke({{0.5, 0.5}, {6, 0}}) == Status::SolidCell);
    CHECK(field.GetDiagnostics().Energy == 0.0);
    REQUIRE(field.TryAddStroke({{-6, 0}, {-3, 0}, 1.5, 0.2, 1.0}) == Status::Success);
    StepInfo info;
    for (usize i = 0; i < 120; ++i)
    {
        REQUIRE(field.TryAdvance(1.0 / 60.0, info) == Status::Success);
    }
    for (usize y = 0; y < config.Height; ++y)
    {
        CHECK_FALSE(field.IsFluid(8, y));
        for (usize x = 9; x < config.Width; ++x)
        {
            CHECK(field.GetCell(x, y).Height == 0.0);
        }
    }
    Sample sample;
    sample.Height = 42.0;
    CHECK(field.TrySample({0.5, 0.5}, sample) == Status::SolidCell);
    CHECK(sample.Height == 42.0);
    CHECK(std::abs(field.GetDiagnostics().IntegratedHeight) < 1e-9);
}

TEST_CASE("Resolution and step budget are explicit and adaptive", "[fluid]")
{
    Field field;
    Config config;
    config.Width = 8;
    config.Height = 6;
    config.HalfExtent = {0.1, 0.1};
    config.MaxSubsteps = 1;
    REQUIRE(field.TryInitialize(config) == Status::Success);
    REQUIRE(field.TryAddStroke({{-0.05, 0}, {0.05, 0}, 0.02, 0.005, 1.0}) == Status::Success);
    float64 limit = 0.0;
    REQUIRE(field.TryGetStepLimit(limit) == Status::Success);
    CHECK(limit > 0.0);
    StepInfo info;
    CHECK(field.TryAdvance(0.1, info) == Status::WorkLimit);
    CHECK(info.Substeps == 1);
    CHECK(info.AdvancedSeconds > 0.0);
    CHECK(info.AdvancedSeconds < 0.1);
    config.MaxSubsteps = 4096;
    REQUIRE(field.TryInitialize(config) == Status::Success);
    std::vector<CellState> initial(48);
    initial[3 * config.Width + 4].Height = 0.1;
    REQUIRE(field.TryReset({}, initial) == Status::Success);
    REQUIRE(field.TryAdvance(0.05, info) == Status::Success);
    CHECK(info.Substeps > 1);
    CHECK(std::isfinite(field.GetDiagnostics().Energy));
    CHECK(field.GetDiagnostics().MinDepth > 0.0);
    CHECK(std::abs(field.GetDiagnostics().IntegratedHeight - 0.1 * (0.2 / 8.0) * (0.2 / 6.0)) < 1e-10);
}

TEST_CASE("A flat resting lake and tiny corner intersections respect the mask", "[fluid]")
{
    Config config;
    config.Width = 4;
    config.Height = 4;
    config.HalfExtent = {2, 2};
    Field field;
    REQUIRE(field.TryInitialize(config) == Status::Success);
    std::array<uint8, 16> mask{};
    mask[2 * 4 + 2] = 1;
    std::array<CellState, 16> initial{};
    for (auto& cell : initial)
    {
        cell.Height = 0.25;
    }
    REQUIRE(field.TryReset(mask, initial) == Status::Success);
    StepInfo info;
    REQUIRE(field.TryAdvance(0.25, info) == Status::Success);
    CHECK(field.GetCell(1, 1).Height == 0.25);
    Sample sample;
    REQUIRE(field.TrySample({-0.5, -0.5}, sample) == Status::Success);
    CHECK(sample.VelocityX == 0.0);
    CHECK(sample.VelocityY == 0.0);
    const auto before = field.GetDiagnostics().Energy;
    // Only a 0.002 m interval lies inside cell (2,2); regular point sampling misses it.
    CHECK(field.TryAddStroke({{-0.5, 0.501}, {0.501, -0.5}}) == Status::SolidCell);
    CHECK(field.GetDiagnostics().Energy == before);
    mask.fill(1);
    REQUIRE(field.TryReset(mask) == Status::Success);
    CHECK(field.GetDiagnostics().MinDepth == 0.0);
    CHECK_FALSE(field.IsActive());
}
