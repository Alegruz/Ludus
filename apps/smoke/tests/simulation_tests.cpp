#include "../internal/simulation.h"
#include <catch2/catch_test_macros.hpp>
using namespace ludus;
namespace
{
using foundation::time::Duration;
using foundation::time::FrameClock;
using foundation::time::FrameSample;
using foundation::time::NANOSECONDS_PER_SECOND;
using foundation::time::Timestamp;
constexpr foundation::uint64 MILLISECOND = NANOSECONDS_PER_SECOND / 1000;
} // namespace
TEST_CASE("Smoke simulation bounds resumed time and ignores hidden/blurred input", "[smoke]")
{
    smoke::Simulation simulation;
    FrameClock clock{smoke::MAXIMUM_FRAME_DELTA};
    platform::browser::WindowState input;
    input.Visible = true;
    input.Focused = true;
    input.Keys[static_cast<foundation::usize>(platform::browser::Key::KeyD)] = true;
    // Startup: the first callback is a zero-elapsed baseline.
    FrameSample frame = smoke::SampleFrame(clock, {0}, input.Visible);
    REQUIRE(frame.IsBaseline);
    smoke::Advance(simulation, input, frame);
    CHECK(simulation.X == 0);
    // A 60 s stall is clamped once by the FrameClock; the excess stays visible.
    frame = smoke::SampleFrame(clock, {60 * NANOSECONDS_PER_SECOND}, input.Visible);
    CHECK(frame.Elapsed == smoke::MAXIMUM_FRAME_DELTA);
    CHECK(frame.Discarded == Duration{60 * NANOSECONDS_PER_SECOND - smoke::MAXIMUM_FRAME_DELTA.Nanoseconds});
    smoke::Advance(simulation, input, frame);
    CHECK(simulation.X == 0.1);
    CHECK(simulation.Phase == 0.1);
    // Hidden callbacks drop the baseline and leave the simulation untouched.
    input.Visible = false;
    frame = smoke::SampleFrame(clock, {120 * NANOSECONDS_PER_SECOND}, input.Visible);
    CHECK(frame.Elapsed == Duration{});
    smoke::Advance(simulation, input, frame);
    CHECK(simulation.X == 0.1);
    CHECK(simulation.Phase == 0.1);
    // Returning visible never replays hidden time; blurred input does not move.
    input.Visible = true;
    input.Focused = false;
    frame = smoke::SampleFrame(clock, {180 * NANOSECONDS_PER_SECOND}, input.Visible);
    CHECK(frame.IsBaseline);
    smoke::Advance(simulation, input, frame);
    CHECK(simulation.Phase == 0.1);
    frame = smoke::SampleFrame(clock, {180 * NANOSECONDS_PER_SECOND + 100 * MILLISECOND}, input.Visible);
    CHECK(frame.Elapsed == Duration{100 * MILLISECOND});
    smoke::Advance(simulation, input, frame);
    CHECK(simulation.X == 0.1);
    CHECK(simulation.Phase == 0.2);
    // A regressed callback contributes nothing.
    frame = smoke::SampleFrame(clock, {MILLISECOND}, input.Visible);
    CHECK(frame.Status == foundation::time::TimeStatus::ClockRegression);
    smoke::Advance(simulation, input, frame);
    CHECK(simulation.Phase == 0.2);
}
TEST_CASE("Smoke pointer uses CSS coordinates regardless of framebuffer density", "[smoke]")
{
    smoke::Simulation simulation;
    platform::browser::WindowState input;
    input.Visible = true;
    input.Focused = true;
    input.CssWidth = 300;
    input.CssHeight = 150;
    input.FramebufferWidth = 450;
    input.FramebufferHeight = 225;
    input.Buttons = 1;
    input.PointerX = 225;
    input.PointerY = 37.5;
    smoke::Advance(simulation, input, {});
    CHECK(simulation.X == 0.5);
    CHECK(simulation.Y == 0.5);
    input.PointerX = 900;
    input.PointerY = -100;
    smoke::Advance(simulation, input, {});
    CHECK(simulation.X == 1);
    CHECK(simulation.Y == 1);
    input.CssWidth = 0;
    input.CssHeight = 0;
    smoke::Advance(simulation, input, {});
    CHECK(simulation.X == 1);
    CHECK(simulation.Phase == 0);
}
