#include <ludus/graphics/rhi/device.h>

#include "internal/backend.h"
#include "internal/device.h"
#include "internal/lifecycle.h"
#include "internal/raster.h"

namespace ludus::graphics::rhi
{
namespace internal
{
struct DeviceAccess final
{
    template <typename T>
    static ludus::foundation::uint64 Owner(T handle) noexcept
    {
        return handle.Owner;
    }

    template <typename T>
    static void Set(T& handle, ludus::foundation::uint64 owner) noexcept
    {
        handle.Owner = owner;
    }
};
} // namespace internal
namespace
{
using internal::DeviceAccess;
internal::DeviceOwnerSequence gOwners;
DeviceDescription gDescription;

// R0 shares the existing registry/loss protocol rather than introducing a second
// resource registry. See docs/architecture/rhi-gdi.md, ownership and identity.
bool Valid(DeviceHandle device) noexcept
{
    return DeviceAccess::Owner(device) != 0 && DeviceAccess::Owner(device) == internal::DeviceOwner();
}
bool Valid(DeviceHandle device, SurfaceHandle surface) noexcept
{
    return Valid(device) && DeviceAccess::Owner(surface) == DeviceAccess::Owner(device);
}
DeviceStatus State() noexcept
{
    switch (GetStartup().State)
    {
        case StartupState::Ready:
            return DeviceStatus::Ready;
        case StartupState::Pending:
            return DeviceStatus::Pending;
        case StartupState::DeviceLost:
            return DeviceStatus::DeviceLost;
        case StartupState::Idle:
        case StartupState::Failed:
            return DeviceStatus::Failed;
    }
    return DeviceStatus::Failed;
}
DeviceStatus FrameResult(FrameStatus status) noexcept
{
    // Preserve terminal startup failure and synchronous acquisition/submission loss.
    const auto state = State();
    if (state == DeviceStatus::Failed || state == DeviceStatus::DeviceLost)
    {
        return state;
    }
    switch (status)
    {
        case FrameStatus::Ready:
            return DeviceStatus::Ready;
        case FrameStatus::Skipped:
            return DeviceStatus::Skipped;
        case FrameStatus::NotReady:
            return DeviceStatus::NotReady;
        case FrameStatus::InvalidState:
            return DeviceStatus::InvalidState;
        case FrameStatus::Unsupported:
            return DeviceStatus::Unsupported;
        case FrameStatus::Failed:
            return DeviceStatus::Failed;
    }
    return DeviceStatus::Failed;
}
ResourceStatus ResourceAdmission(DeviceHandle device) noexcept
{
    if (!Valid(device))
    {
        return ResourceStatus::InvalidHandle;
    }
    const auto state = State();
    return state == DeviceStatus::DeviceLost ? ResourceStatus::DeviceLost
           : state == DeviceStatus::Ready    ? ResourceStatus::Ready
                                             : ResourceStatus::NotReady;
}
ResourceStatus ResourceResult(ResourceStatus result) noexcept
{
    return State() == DeviceStatus::DeviceLost ? ResourceStatus::DeviceLost : result;
}
} // namespace
DeviceStatus ValidateDeviceDescription(const DeviceDescription& description) noexcept
{
    if (description.Selection != BackendSelection::Auto && description.Selection != BackendSelection::WebGPU &&
        description.Selection != BackendSelection::WebGL2)
    {
        return DeviceStatus::InvalidDescription;
    }
    return description.Required.Compute || description.Required.IndirectRendering ? DeviceStatus::Unsupported
                                                                                  : DeviceStatus::Ready;
}
BackendAvailability GetCompiledBackends() noexcept
{
    const auto kind = backend::Kind();
    return
    {
        .Vulkan = kind == Backend::Vulkan,
        .Metal = kind == Backend::Metal,
        .WebGPU = backend::Supports(BackendSelection::WebGPU),
        .WebGL2 = backend::Supports(BackendSelection::WebGL2),
    };
}
DeviceStatus CreateDevice(const ApplicationInfo& application,
                          const WindowInfo& window,
                          const DeviceDescription& description,
                          DeviceHandle& device,
                          SurfaceHandle& surface,
                          StartupInfo* failure) noexcept
{
    if (DeviceAccess::Owner(device) != 0 || DeviceAccess::Owner(surface) != 0 || internal::SessionBusy())
    {
        return DeviceStatus::InvalidState;
    }
    const auto admission = ValidateDeviceDescription(description);
    if (admission != DeviceStatus::Ready)
    {
        return admission;
    }
    if (gOwners.Next == 0)
    {
        return DeviceStatus::IdentityExhausted;
    }
    const auto result = internal::StartOwned(application,
                                             window,
                                             description.Selection,
                                             description.Limits,
                                             description.Required.PortableRaster);
    if (result == StartStatus::Busy)
    {
        return DeviceStatus::InvalidState;
    }
    if (result == StartStatus::Failed)
    {
        if (failure != nullptr)
        {
            *failure = GetStartup();
        }
        Shutdown();
        return DeviceStatus::Failed;
    }
    const auto owner = gOwners.Take();
    internal::SetDeviceOwner(owner);
    gDescription = description;
    DeviceAccess::Set(device, owner);
    DeviceAccess::Set(surface, owner);
    return result == StartStatus::Ready ? DeviceStatus::Ready : DeviceStatus::Pending;
}
DeviceStatus GetDeviceInfo(DeviceHandle device, DeviceInfo& info) noexcept
{
    if (!Valid(device))
    {
        return DeviceStatus::InvalidHandle;
    }
    info = {};
    info.Compiled = GetCompiledBackends();
    info.Startup = GetStartup();
    info.Description = gDescription;
    const auto state = State();
    if (state == DeviceStatus::Ready)
    {
        info.Supported.FullscreenRaster = true;
        info.Enabled.FullscreenRaster = true;
        info.Supported.PortableRaster = backend::RasterLimits().MaxUniformRange >= 16384;
        info.Enabled.PortableRaster = info.Supported.PortableRaster;
    }
    return state;
}
DeviceStatus DestroyDevice(DeviceHandle& device) noexcept
{
    if (!Valid(device))
    {
        return DeviceStatus::InvalidHandle;
    }
    Shutdown();
    device = {};
    return DeviceStatus::Ready;
}
DeviceStatus SetFrameTarget(DeviceHandle device, SurfaceHandle surface, const FrameTarget& target) noexcept
{
    return Valid(device, surface) ? FrameResult(SetFrameTarget(target)) : DeviceStatus::InvalidHandle;
}
DeviceStatus BeginFrame(DeviceHandle device, SurfaceHandle surface) noexcept
{
    return Valid(device, surface) ? FrameResult(BeginFrameStatus()) : DeviceStatus::InvalidHandle;
}
DeviceStatus EndFrame(DeviceHandle device) noexcept
{
    return Valid(device) ? FrameResult(EndFrameStatus()) : DeviceStatus::InvalidHandle;
}
DeviceStatus GetFrameInfo(DeviceHandle device, SurfaceHandle surface, FrameInfo& info) noexcept
{
    if (!Valid(device, surface))
    {
        return DeviceStatus::InvalidHandle;
    }
    const auto state = State();
    if (state != DeviceStatus::Ready)
    {
        return state == DeviceStatus::Pending ? DeviceStatus::NotReady : state;
    }
    const auto acquired = GetFrameInfo();
    if (acquired.Width == 0 || acquired.Height == 0)
    {
        return DeviceStatus::InvalidState;
    }
    info = acquired;
    return DeviceStatus::Ready;
}
ResourceStatus CreateShader(DeviceHandle device, const ShaderDescription& description, ShaderHandle& shader) noexcept
{
    const auto admission = ResourceAdmission(device);
    return admission == ResourceStatus::Ready ? ResourceResult(CreateShader(description, shader)) : admission;
}
ResourceStatus CreateUniform(DeviceHandle device, foundation::usize size, UniformHandle& uniform) noexcept
{
    const auto admission = ResourceAdmission(device);
    return admission == ResourceStatus::Ready ? ResourceResult(CreateUniform(size, uniform)) : admission;
}
ResourceStatus
CreatePipeline(DeviceHandle device, const PipelineDescription& description, PipelineHandle& pipeline) noexcept
{
    const auto admission = ResourceAdmission(device);
    return admission == ResourceStatus::Ready ? ResourceResult(CreatePipeline(description, pipeline)) : admission;
}
ResourceStatus GetStatus(DeviceHandle device, ShaderHandle shader) noexcept
{
    const auto admission = ResourceAdmission(device);
    return admission == ResourceStatus::Ready ? ResourceResult(GetStatus(shader)) : admission;
}
ResourceStatus GetStatus(DeviceHandle device, UniformHandle uniform) noexcept
{
    const auto admission = ResourceAdmission(device);
    return admission == ResourceStatus::Ready ? ResourceResult(GetStatus(uniform)) : admission;
}
ResourceStatus GetStatus(DeviceHandle device, PipelineHandle pipeline) noexcept
{
    const auto admission = ResourceAdmission(device);
    return admission == ResourceStatus::Ready ? ResourceResult(GetStatus(pipeline)) : admission;
}
ResourceStatus
UpdateUniform(DeviceHandle device, UniformHandle uniform, std::span<const foundation::uint8> bytes) noexcept
{
    const auto admission = ResourceAdmission(device);
    return admission == ResourceStatus::Ready ? ResourceResult(UpdateUniform(uniform, bytes)) : admission;
}
ResourceStatus Destroy(DeviceHandle device, ShaderHandle shader) noexcept
{
    const auto admission = ResourceAdmission(device);
    return admission == ResourceStatus::Ready ? ResourceResult(Destroy(shader)) : admission;
}
ResourceStatus Destroy(DeviceHandle device, UniformHandle uniform) noexcept
{
    const auto admission = ResourceAdmission(device);
    return admission == ResourceStatus::Ready ? ResourceResult(Destroy(uniform)) : admission;
}
ResourceStatus Destroy(DeviceHandle device, PipelineHandle pipeline) noexcept
{
    const auto admission = ResourceAdmission(device);
    return admission == ResourceStatus::Ready ? ResourceResult(Destroy(pipeline)) : admission;
}
ResourceStatus DrawFullscreen(DeviceHandle device, PipelineHandle pipeline) noexcept
{
    const auto admission = ResourceAdmission(device);
    return admission == ResourceStatus::Ready ? ResourceResult(DrawFullscreen(pipeline)) : admission;
}
} // namespace ludus::graphics::rhi
