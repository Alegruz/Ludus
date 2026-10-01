#include "internal/simulation.h"
namespace ludus::smoke
{
namespace
{
using namespace foundation;
float64 Bound(float64 value) noexcept
{
    return value < -1 ? -1 : (value > 1 ? 1 : value);
}
} // namespace
void Advance(Simulation& simulation, const platform::browser::WindowState& input, float64 delta) noexcept
{
    if (!input.Visible)
    {
        return;
    }
    if (!(delta >= 0))
    {
        delta = 0;
    }
    if (delta > 0.1)
    {
        delta = 0.1;
    }
    simulation.Phase += delta;
    if (simulation.Phase >= 2)
    {
        simulation.Phase -= 2;
    }
    if (!input.Focused)
    {
        return;
    }
    using platform::browser::Key;
    const auto held = [&input](Key key) noexcept { return input.Keys[static_cast<foundation::usize>(key)]; };
    simulation.X = Bound(simulation.X + delta * ((held(Key::KeyD) || held(Key::ArrowRight) ? 1 : 0) -
                                                 (held(Key::KeyA) || held(Key::ArrowLeft) ? 1 : 0)));
    simulation.Y = Bound(simulation.Y + delta * ((held(Key::KeyW) || held(Key::ArrowUp) ? 1 : 0) -
                                                 (held(Key::KeyS) || held(Key::ArrowDown) ? 1 : 0)));
    if ((input.Buttons & 1U) != 0 && input.CssWidth > 0 && input.CssHeight > 0)
    {
        simulation.X = Bound(input.PointerX * 2 / input.CssWidth - 1);
        simulation.Y = Bound(1 - input.PointerY * 2 / input.CssHeight);
    }
}
} // namespace ludus::smoke
