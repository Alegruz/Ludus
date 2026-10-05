#pragma once

#include <ludus/foundation/base/types.h>

#include <span>
#include <string_view>

namespace ludus::graphics::rhi
{
namespace internal
{
struct ResourceAccess;
}
// Values identify one owned resource in one session, never backend objects.
// Copying a handle does not copy ownership. Destroy exactly once, between frames.
struct ShaderHandle final
{
private:
    ludus::foundation::uint32 Id = 0;
    friend struct internal::ResourceAccess;
};
struct UniformHandle final
{
private:
    ludus::foundation::uint32 Id = 0;
    friend struct internal::ResourceAccess;
};
struct PipelineHandle final
{
private:
    ludus::foundation::uint32 Id = 0;
    friend struct internal::ResourceAccess;
};
enum class ResourceStatus : ludus::foundation::uint8
{
    Ready,
    Pending,
    Failed,
    NotReady,
    InvalidHandle,
    InvalidState,
    InvalidDescription,
    CapacityExceeded,
    InUse
};
enum class ShaderStage : ludus::foundation::uint8
{
    Vertex,
    Fragment
};
// Artifact storage is borrowed only during CreateShader. Entries are explicit
// per target (Slang's SPIR-V and WGSL entry names can differ, and the generated
// GLSL ES stage always links through main). GlslEs is an additional browser
// backend artifact (WebGL 2); it is only populated for web builds and never
// exposes a backend object. A browser build may carry both Wgsl and GlslEs so
// the engine selects WebGPU or WebGL 2 at startup.
struct ShaderDescription final
{
    ShaderStage Stage = ShaderStage::Vertex;
    // Minimum occupied bytes from the selected artifact's reflection.
    ludus::foundation::usize UniformSize = 0;
    std::span<const ludus::foundation::uint32> Spirv;
    std::string_view Wgsl;
    std::string_view GlslEs = "";
    std::string_view SpirvEntry;
    std::string_view WgslEntry;
    std::string_view GlslEsEntry = "";
};
// One binding: set/group 0, binding 0, visible to vertex and fragment.
struct PipelineDescription final
{
    ShaderHandle Vertex;
    ShaderHandle Fragment;
    UniformHandle Uniform;
};
// Bounded slice: eight resources of each kind; uniform size 16..16384, multiple
// of 16, further bounded by StartupInfo.Capabilities.MaxUniformBufferSize.
// Upload bytes follow the application's independently verified layouts.
[[nodiscard]] ResourceStatus CreateShader(const ShaderDescription&, ShaderHandle&) noexcept;
[[nodiscard]] ResourceStatus CreateUniform(ludus::foundation::usize size, UniformHandle&) noexcept;
// Pending means the output owns a resource. Pending dependencies instead return
// NotReady without creating a pipeline; poll those dependencies before retrying.
[[nodiscard]] ResourceStatus CreatePipeline(const PipelineDescription&, PipelineHandle&) noexcept;
[[nodiscard]] ResourceStatus GetStatus(ShaderHandle) noexcept;
[[nodiscard]] ResourceStatus GetStatus(UniformHandle) noexcept;
[[nodiscard]] ResourceStatus GetStatus(PipelineHandle) noexcept;
enum class SurfaceEncoding : ludus::foundation::uint8
{
    Unorm,
    Srgb
};
struct FrameInfo final
{
    ludus::foundation::uint32 Width = 0;
    ludus::foundation::uint32 Height = 0;
    SurfaceEncoding Encoding = SurfaceEncoding::Unorm;
};
// Actual acquired extent/attachment conversion, valid only in an open frame.
[[nodiscard]] FrameInfo GetFrameInfo() noexcept;
// Full replacement between frames or before the open frame's first draw.
// No allocation; latest bytes are snapshotted
// into the next acquired frame. A uniform must be updated before its first draw.
[[nodiscard]] ResourceStatus UpdateUniform(UniformHandle, std::span<const ludus::foundation::uint8>) noexcept;
[[nodiscard]] ResourceStatus Destroy(ShaderHandle) noexcept;
[[nodiscard]] ResourceStatus Destroy(UniformHandle) noexcept;
[[nodiscard]] ResourceStatus Destroy(PipelineHandle) noexcept;
// Exactly one non-indexed triangle in an open frame, current full surface extent.
// Pending/failed/stale resources and a second draw are rejected.
[[nodiscard]] ResourceStatus DrawFullscreen(PipelineHandle) noexcept;
} // namespace ludus::graphics::rhi
