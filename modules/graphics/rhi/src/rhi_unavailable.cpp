#include "internal/backend.h"
#include "internal/resources.h"

#include <ludus/foundation/base/config.h>

#if !defined(LUDUS_PLATFORM_MACOS)
#    error "The unavailable Metal backend is reserved for macOS builds"
#endif

// Compile-only macOS boundary. Replace with the Metal backend when implemented;
// an unavailable renderer must fail explicitly and must never claim Ready.
namespace ludus::graphics::rhi::backend
{
using ludus::foundation::uint32;
using ludus::foundation::uint8;
using ludus::foundation::usize;
Backend Kind() noexcept
{
    return Backend::Metal;
}
bool Supports(BackendSelection selection) noexcept
{
    return selection == BackendSelection::Auto;
}
StartupError Start(const ApplicationInfo&, const WindowInfo&, uint32, BackendSelection) noexcept
{
    return StartupError::BackendUnavailable;
}
bool Initialize(const ApplicationInfo&) noexcept
{
    return false;
}
bool ConnectWindow(const WindowInfo&) noexcept
{
    return false;
}
bool InitializeRendering() noexcept
{
    return false;
}
void ShutdownRendering() noexcept {}
void Shutdown() noexcept {}
bool BeginFrame() noexcept
{
    return false;
}
bool EndFrame() noexcept
{
    return false;
}
FrameStatus SetTarget(const FrameTarget&) noexcept
{
    return FrameStatus::Unsupported;
}
FrameStatus Begin() noexcept
{
    return FrameStatus::NotReady;
}
FrameStatus End() noexcept
{
    return FrameStatus::NotReady;
}
ResourceStatus CreateShader(usize, const ShaderDescription&, uint32) noexcept
{
    return ResourceStatus::NotReady;
}
ResourceStatus CreateUniform(usize, const UniformDescription&, uint32) noexcept
{
    return ResourceStatus::NotReady;
}
ResourceStatus CreatePipeline(usize, const PipelineResources&, uint32) noexcept
{
    return ResourceStatus::NotReady;
}
void DestroyShader(usize) noexcept {}
void DestroyUniform(usize) noexcept {}
void DestroyPipeline(usize) noexcept {}
void UpdateUniform(usize, std::span<const uint8>) noexcept {}
FrameInfo GetFrameInfo() noexcept
{
    return {};
}
ResourceStatus Draw(usize) noexcept
{
    return ResourceStatus::NotReady;
}
} // namespace ludus::graphics::rhi::backend
