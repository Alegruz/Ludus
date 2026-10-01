#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/platform/browser/window.h>

#include <ludus/graphics/rhi/rhi.h>

#include "simulation.h"

namespace ludus::smoke::renderer
{
enum class State : foundation::uint8
{
    Loading,
    Ready,
    Failed
};
State Prepare() noexcept;
graphics::rhi::FrameStatus Render(const platform::browser::WindowState&, const Simulation&) noexcept;
void Shutdown() noexcept;
} // namespace ludus::smoke::renderer
