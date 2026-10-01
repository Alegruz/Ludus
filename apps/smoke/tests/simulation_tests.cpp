#include "../internal/simulation.h"
#include <catch2/catch_test_macros.hpp>
using namespace ludus;
TEST_CASE("Smoke simulation bounds resumed time and ignores hidden/blurred input", "[smoke]")
{
    smoke::Simulation simulation;
    platform::browser::WindowState input;
    input.Visible = true;
    input.Focused = true;
    input.Keys[static_cast<foundation::usize>(platform::browser::Key::KeyD)] = true;
    smoke::Advance(simulation, input, 60);
    CHECK(simulation.X == 0.1);
    CHECK(simulation.Phase == 0.1);
    input.Visible = false;
    smoke::Advance(simulation, input, 60);
    CHECK(simulation.X == 0.1);
    CHECK(simulation.Phase == 0.1);
    input.Visible = true;
    input.Focused = false;
    smoke::Advance(simulation, input, 0.1);
    CHECK(simulation.X == 0.1);
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
    smoke::Advance(simulation, input, 0);
    CHECK(simulation.X == 0.5);
    CHECK(simulation.Y == 0.5);
    input.PointerX = 900;
    input.PointerY = -100;
    smoke::Advance(simulation, input, 0);
    CHECK(simulation.X == 1);
    CHECK(simulation.Y == 1);
    input.CssWidth = 0;
    input.CssHeight = 0;
    smoke::Advance(simulation, input, -1);
    CHECK(simulation.X == 1);
    CHECK(simulation.Phase == 0);
}
