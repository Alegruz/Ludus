#include <ludus/graphics/rhi/lifetime.h>

#include "internal/lifecycle.h"
#include "internal/raster.h"

#include <ludus/foundation/containers/array.hpp>

#include <cstring>

// Thanks to Khronos, "WebGL 2.0 Specification", sections 3.7.3 (buffer objects),
// 3.7.6 (textures) and 3.7.9 (drawing buffers), for the deliberately explicit
// range/packing and mesh-local indexed-draw boundaries:
// https://registry.khronos.org/webgl/specs/latest/2.0/
// Ownership/retention follows docs/architecture/rhi-gdi.md; this is the RHI's
// single portable registry. Backend objects never become public identities.
namespace ludus::graphics::rhi
{
namespace internal
{
struct RasterIdentity final
{
    foundation::uint64 Owner;
    foundation::usize Slot;
    foundation::uint64 Generation;
};
struct RasterAccess final
{
    template <typename T>
    static foundation::uint64 Owner(T handle) noexcept
    {
        return handle.Owner;
    }
    template <typename T>
    static foundation::uint64 Generation(T handle) noexcept
    {
        return handle.Generation;
    }
    template <typename T>
    static foundation::usize Slot(T handle) noexcept
    {
        return handle.Slot == 0 ? RASTER_CAPACITY : handle.Slot - 1;
    }
    template <typename T>
    static void Set(T& handle, const RasterIdentity& identity) noexcept
    {
        handle.Owner = identity.Owner;
        handle.Slot = static_cast<foundation::uint32>(identity.Slot + 1);
        handle.Generation = identity.Generation;
    }
};
} // namespace internal

namespace
{
using namespace foundation;
using namespace internal;
constexpr usize KINDS = static_cast<usize>(RasterKind::Count);
struct Record final
{
    uint64 Owner = 0;
    uint64 Generation = 1;
    uint64 LastUse = 0;
    uint32 Request = 0;
    uint32 Callbacks = 0;
    usize References = 0;
    RasterStatus Status = RasterStatus::Failed;
    bool Published = false;
    bool InFrame = false;
    bool ServiceOwned = false;
    bool QueuedPipeline = false;
    BufferDescription Buffer;
    TextureDescription Texture;
    usize TextureSlot = 0;
    RasterShaderInfo Shader;
    RasterLayout Layout;
    RasterSet Set;
    RasterPipelineInfo Pipeline;
    Array<uint8> Indices;
};
Record gRecords[KINDS][RASTER_CAPACITY];
RasterSequence<uint32> gRequests;
RasterSequence<uint64> gSubmissions;
uint64 gFrameSubmission = 0;
uint64 gFirstAcceptedSubmission = 0;
uint64 gLastAcceptedSubmission = 0;
usize gDrawCount = 0;

Record& At(RasterKind kind, usize slot) noexcept
{
    return gRecords[static_cast<usize>(kind)][slot];
}
RasterStatus Admission(DeviceHandle device, bool mutation = true) noexcept
{
    DeviceInfo info;
    switch (GetDeviceInfo(device, info))
    {
        case DeviceStatus::InvalidHandle:
            return RasterStatus::InvalidHandle;
        case DeviceStatus::Ready:
            break;
        case DeviceStatus::Pending:
            return RasterStatus::NotReady;
        case DeviceStatus::DeviceLost:
            return RasterStatus::DeviceLost;
        default:
            return RasterStatus::Failed;
    }
    if (!info.Enabled.PortableRaster)
    {
        return RasterStatus::Unsupported;
    }
    return mutation && FrameOpen() ? RasterStatus::InvalidState : RasterStatus::Ready;
}
template <typename T>
Record* Resolve(T handle, RasterKind kind, bool published = true) noexcept
{
    const auto slot = RasterAccess::Slot(handle);
    if (slot >= RASTER_CAPACITY || RasterAccess::Owner(handle) == 0)
    {
        return nullptr;
    }
    auto& record = At(kind, slot);
    return record.Owner == DeviceOwner() && record.Owner == RasterAccess::Owner(handle) &&
                   record.Generation == RasterAccess::Generation(handle) && (!published || record.Published)
               ? &record
               : nullptr;
}
void Retain(RasterKind kind, usize slot) noexcept
{
    ++At(kind, slot).References;
}
void Release(RasterKind kind, usize slot) noexcept
{
    --At(kind, slot).References;
}
void Dependencies(Record& record, RasterKind kind, bool retain) noexcept
{
    const auto reference = [retain](RasterKind dependency, usize slot) noexcept {
        if (retain)
        {
            Retain(dependency, slot);
        }
        else
        {
            Release(dependency, slot);
        }
    };
    if (kind == RasterKind::View)
    {
        reference(RasterKind::Texture, record.TextureSlot);
    }
    else if (kind == RasterKind::Pipeline)
    {
        reference(RasterKind::Shader, record.Pipeline.Vertex);
        reference(RasterKind::Shader, record.Pipeline.Fragment);
        reference(RasterKind::Layout, record.Pipeline.Layout);
    }
    else if (kind == RasterKind::Set)
    {
        const auto& layout = At(RasterKind::Layout, record.Set.Layout).Layout;
        for (usize i = 0; i < layout.Count; ++i)
        {
            const auto type = layout.Entries[i].Kind;
            reference(type == RasterBindingKind::UniformBuffer ? RasterKind::Buffer
                      : type == RasterBindingKind::Texture2D   ? RasterKind::View
                                                               : RasterKind::Sampler,
                      record.Set.Slots[i]);
        }
        reference(RasterKind::Layout, record.Set.Layout);
    }
}
void Collect() noexcept
{
    const auto completed = backend::RasterCompleted();
    // Acyclic dependencies can expose another detached record in each pass.
    for (usize pass = 0; pass < KINDS; ++pass)
    {
        for (usize kind = KINDS; kind-- > 0;)
        {
            for (usize slot = 0; slot < RASTER_CAPACITY; ++slot)
            {
                auto& record = gRecords[kind][slot];
                if (record.Owner == 0 || record.References != 0 || record.Status == RasterStatus::Pending ||
                    record.Callbacks != 0 || record.LastUse > completed)
                {
                    continue;
                }
                backend::RasterDestroy(static_cast<RasterKind>(kind), slot);
                Dependencies(record, static_cast<RasterKind>(kind), false);
                const auto generation = RasterNextGeneration(record.Generation);
                record = {};
                record.Generation = generation;
            }
        }
    }
}
RasterStatus Reserve(RasterKind kind, usize& slot) noexcept
{
    Collect();
    if (GetStartup().State != StartupState::Ready)
    {
        return GetStartup().State == StartupState::DeviceLost ? RasterStatus::DeviceLost : RasterStatus::Failed;
    }
    bool exhausted = false;
    for (slot = 0; slot < RASTER_CAPACITY; ++slot)
    {
        auto& record = At(kind, slot);
        if (record.Owner != 0)
        {
            continue;
        }
        if (record.Generation == 0)
        {
            exhausted = true;
            continue;
        }
        if (gRequests.Next == 0)
        {
            return RasterStatus::IdentityExhausted;
        }
        record.Owner = DeviceOwner();
        record.References = 1;
        record.Request = gRequests.Take();
        record.Status = RasterStatus::Pending;
        return RasterStatus::Ready;
    }
    return exhausted ? RasterStatus::IdentityExhausted : RasterStatus::CapacityExceeded;
}
template <typename T, typename Create>
RasterStatus Publish(RasterKind kind, T& output, Create create) noexcept
{
    if (RasterAccess::Owner(output) != 0)
    {
        return RasterStatus::InvalidState;
    }
    usize slot = 0;
    const auto reservation = Reserve(kind, slot);
    if (reservation != RasterStatus::Ready)
    {
        return reservation;
    }
    auto& record = At(kind, slot);
    const auto result = create(slot, record);
    if (record.Owner == 0)
    {
        return GetStartup().State == StartupState::DeviceLost ? RasterStatus::DeviceLost : RasterStatus::Failed;
    }
    // Spontaneous error-scope callbacks may have updated Status inside create.
    if (result != RasterStatus::Ready && result != RasterStatus::Pending)
    {
        backend::RasterDestroy(kind, slot);
        const auto generation = record.Generation;
        record = {};
        record.Generation = generation;
        return result;
    }
    if (record.Status == RasterStatus::Pending)
    {
        record.Status = result;
    }
    Dependencies(record, kind, true);
    record.Published = true;
    RasterAccess::Set(output, {record.Owner, slot, record.Generation});
    return result;
}
bool ValidBinding(const RasterBinding& binding) noexcept
{
    const auto visibility = static_cast<uint8>(binding.Visibility);
    return binding.Binding < RASTER_BINDINGS && visibility >= 1 && visibility <= 3 &&
           (binding.Kind == RasterBindingKind::UniformBuffer
                ? binding.MinSize > 0 && binding.MinSize <= 16384
                : (binding.Kind == RasterBindingKind::Texture2D || binding.Kind == RasterBindingKind::Sampler) &&
                      binding.MinSize == 0);
}
bool CopyLayout(std::span<const RasterBinding> entries, RasterLayout& layout) noexcept
{
    if (entries.size() > RASTER_BINDINGS)
    {
        return false;
    }
    layout.Count = entries.size();
    for (usize i = 0; i < entries.size(); ++i)
    {
        if (!ValidBinding(entries[i]))
        {
            return false;
        }
        for (usize j = 0; j < i; ++j)
        {
            if (entries[i].Binding == entries[j].Binding)
            {
                return false;
            }
        }
        layout.Entries[i] = entries[i];
    }
    return true;
}
bool CopyName(std::string_view name, char (&output)[64]) noexcept
{
    if (name.size() >= 64)
    {
        return false;
    }
    if (name.empty())
    {
        return true;
    }
    for (const char c : name)
    {
        if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && (c < '0' || c > '9') && c != '_')
        {
            return false;
        }
    }
    std::memcpy(output, name.data(), name.size());
    return true;
}
bool Fits(usize offset, usize size, usize total) noexcept
{
    return offset <= total && size <= total - offset;
}
void FrameRetain(RasterKind kind, usize slot) noexcept
{
    auto& record = At(kind, slot);
    if (!record.InFrame)
    {
        record.InFrame = true;
        ++record.References;
    }
}
bool ShaderFits(const RasterShaderInfo& shader, const RasterLayout& layout) noexcept
{
    for (usize i = 0; i < shader.Layout.Count; ++i)
    {
        const auto& need = shader.Layout.Entries[i];
        bool found = false;
        const auto stage = shader.Stage == ShaderStage::Vertex ? 1U : 2U;
        for (usize j = 0; j < layout.Count; ++j)
        {
            const auto& entry = layout.Entries[j];
            if (entry.Binding == need.Binding && entry.Kind == need.Kind && entry.MinSize >= need.MinSize &&
                (static_cast<uint8>(entry.Visibility) & stage) != 0)
            {
                found = true;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}
} // namespace
RasterStatus GetRasterCapabilities(DeviceHandle device, RasterCapabilities& output) noexcept
{
    const auto admission = Admission(device, false);
    if (admission == RasterStatus::InvalidHandle)
    {
        return admission;
    }
    output = admission == RasterStatus::Ready ? backend::RasterLimits() : RasterCapabilities{};
    return admission;
}
RasterStatus CreateBuffer(DeviceHandle device,
                          const BufferDescription& description,
                          std::span<const uint8> bytes,
                          BufferHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto limits = backend::RasterLimits();
    if (description.Size == 0 || bytes.size() != description.Size || description.Size > limits.MaxBufferSize ||
        static_cast<uint8>(description.Role) > static_cast<uint8>(BufferRole::Uniform))
    {
        return RasterStatus::InvalidDescription;
    }
    const usize alignment = description.Role == BufferRole::Index16   ? 2
                            : description.Role == BufferRole::Uniform ? 16
                                                                      : 4;
    if (description.Size % alignment != 0)
    {
        return RasterStatus::InvalidDescription;
    }
    return Publish(RasterKind::Buffer, output, [&](usize slot, Record& record) noexcept {
        record.Buffer = description;
        if (description.Role == BufferRole::Index16 || description.Role == BufferRole::Index32)
        {
            if (!record.Indices.TryResize(bytes.size()))
            {
                return RasterStatus::OutOfMemory;
            }
            std::memcpy(record.Indices.GetData(), bytes.data(), bytes.size());
        }
        return backend::RasterCreateBuffer(slot, description, bytes, record.Request);
    });
}
RasterStatus CreateTexture(DeviceHandle device,
                           const TextureDescription& description,
                           const TextureUpload& upload,
                           TextureHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto textureLimits = backend::RasterLimits();
    const auto maxDimension = textureLimits.MaxTextureDimension2D;
    if (description.Width == 0 || description.Height == 0 || description.Width > maxDimension ||
        description.Height > maxDimension ||
        (description.Format != RasterFormat::Rgba8Unorm && description.Format != RasterFormat::Rgba8Srgb))
    {
        return RasterStatus::InvalidDescription;
    }
    const usize row = static_cast<usize>(description.Width) * 4;
    if (upload.RowPitch < row || upload.RowPitch % 4 != 0 || upload.RowPitch > ~usize{0} / description.Height ||
        upload.Bytes.size() != upload.RowPitch * description.Height ||
        upload.Bytes.size() > textureLimits.MaxBufferSize)
    {
        return RasterStatus::InvalidDescription;
    }
    return Publish(RasterKind::Texture, output, [&](usize slot, Record& record) noexcept {
        record.Texture = description;
        return backend::RasterCreateTexture(slot, description, upload, record.Request);
    });
}
RasterStatus CreateTextureView(DeviceHandle device, TextureHandle texture, TextureViewHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto* source = Resolve(texture, RasterKind::Texture);
    if (source == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (source->Status != RasterStatus::Ready)
    {
        return source->Status == RasterStatus::Pending ? RasterStatus::NotReady : source->Status;
    }
    return Publish(RasterKind::View, output, [&](usize slot, Record& record) noexcept {
        record.TextureSlot = RasterAccess::Slot(texture);
        return backend::RasterCreateView(slot, {record.TextureSlot}, record.Request);
    });
}
RasterStatus CreateSampler(DeviceHandle device, const SamplerDescription& description, SamplerHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    if (static_cast<uint8>(description.Filter) > 1 || static_cast<uint8>(description.U) > 1 ||
        static_cast<uint8>(description.V) > 1)
    {
        return RasterStatus::InvalidDescription;
    }
    return Publish(RasterKind::Sampler, output, [&](usize slot, Record& record) noexcept {
        return backend::RasterCreateSampler(slot, description, record.Request);
    });
}
RasterStatus
CreateRasterShader(DeviceHandle device, const RasterShaderDescription& description, RasterShaderHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    RasterShaderInfo info;
    info.Stage = description.Artifact.Stage;
    if ((info.Stage != ShaderStage::Vertex && info.Stage != ShaderStage::Fragment) ||
        !CopyLayout(description.Bindings, info.Layout))
    {
        return RasterStatus::InvalidDescription;
    }
    for (usize i = 0; i < RASTER_BINDINGS; ++i)
    {
        if (!CopyName(description.UniformBlocks[i], info.UniformBlocks[i]) ||
            !CopyName(description.TextureNames[i], info.TextureNames[i]) ||
            description.TextureSamplers[i] >= RASTER_BINDINGS)
        {
            return RasterStatus::InvalidDescription;
        }
        info.TextureSamplers[i] = description.TextureSamplers[i];
    }
    if (description.Inputs.size() > 8 || (info.Stage == ShaderStage::Fragment && !description.Inputs.empty()))
    {
        return RasterStatus::InvalidDescription;
    }
    info.InputCount = description.Inputs.size();
    for (usize i = 0; i < info.InputCount; ++i)
    {
        const auto& input = description.Inputs[i];
        if (input.Location >= 8 || static_cast<uint8>(input.Format) > 2)
        {
            return RasterStatus::InvalidDescription;
        }
        for (usize j = 0; j < i; ++j)
        {
            if (info.Inputs[j].Location == input.Location)
            {
                return RasterStatus::InvalidDescription;
            }
        }
        info.Inputs[i] = input;
    }
    const auto& artifact = description.Artifact;
    const auto backendKind = GetStartup().SelectedBackend;
    if (backendKind == Backend::WebGL2)
    {
        for (usize i = 0; i < info.Layout.Count; ++i)
        {
            const auto& binding = info.Layout.Entries[i];
            if (binding.Kind != RasterBindingKind::Texture2D)
            {
                continue;
            }
            bool samplerFound = false;
            for (usize j = 0; j < info.Layout.Count; ++j)
            {
                const auto& candidate = info.Layout.Entries[j];
                samplerFound |= candidate.Kind == RasterBindingKind::Sampler &&
                                candidate.Binding == info.TextureSamplers[binding.Binding];
            }
            if (!samplerFound)
            {
                return RasterStatus::InvalidDescription;
            }
        }
    }
    const auto entry = backendKind == Backend::Metal    ? artifact.MslEntry
                       : backendKind == Backend::WebGPU ? artifact.WgslEntry
                       : backendKind == Backend::WebGL2 ? artifact.GlslEsEntry
                                                        : artifact.SpirvEntry;
    char name[64]{};
    if (entry.empty() || !CopyName(entry, name) ||
        (backendKind == Backend::Metal    ? artifact.Msl.empty()
         : backendKind == Backend::WebGPU ? artifact.Wgsl.empty()
         : backendKind == Backend::WebGL2 ? artifact.GlslEs.empty()
                                          : artifact.Spirv.size() < 5 || artifact.Spirv[0] != 0x07230203U))
    {
        return RasterStatus::InvalidDescription;
    }
    return Publish(RasterKind::Shader, output, [&](usize slot, Record& record) noexcept {
        record.Shader = info;
        return backend::RasterCreateShader(slot, artifact, info, record.Request);
    });
}
RasterStatus
CreateBindingLayout(DeviceHandle device, std::span<const RasterBinding> entries, BindingLayoutHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    RasterLayout layout;
    if (!CopyLayout(entries, layout))
    {
        return RasterStatus::InvalidDescription;
    }
    return Publish(RasterKind::Layout, output, [&](usize slot, Record& record) noexcept {
        record.Layout = layout;
        return backend::RasterCreateLayout(slot, layout, record.Request);
    });
}
RasterStatus CreateBindingSet(DeviceHandle device,
                              BindingLayoutHandle handle,
                              std::span<const RasterBindingResource> entries,
                              BindingSetHandle& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto* layoutRecord = Resolve(handle, RasterKind::Layout);
    if (layoutRecord == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (layoutRecord->Status != RasterStatus::Ready)
    {
        return layoutRecord->Status == RasterStatus::Pending ? RasterStatus::NotReady : layoutRecord->Status;
    }
    const auto& layout = layoutRecord->Layout;
    if (entries.size() != layout.Count)
    {
        return RasterStatus::InvalidDescription;
    }
    RasterSet set;
    set.Layout = RasterAccess::Slot(handle);
    const auto limits = backend::RasterLimits();
    for (usize i = 0; i < layout.Count; ++i)
    {
        const RasterBindingResource* entry = nullptr;
        for (const auto& candidate : entries)
        {
            if (candidate.Binding != layout.Entries[i].Binding)
            {
                continue;
            }
            if (entry != nullptr)
            {
                return RasterStatus::InvalidDescription;
            }
            entry = &candidate;
        }
        if (entry == nullptr)
        {
            return RasterStatus::InvalidDescription;
        }
        const auto kind = layout.Entries[i].Kind;
        const auto* resource = kind == RasterBindingKind::UniformBuffer ? Resolve(entry->Buffer, RasterKind::Buffer)
                               : kind == RasterBindingKind::Texture2D   ? Resolve(entry->Texture, RasterKind::View)
                                                                        : Resolve(entry->Sampler, RasterKind::Sampler);
        if (resource == nullptr)
        {
            return RasterStatus::InvalidHandle;
        }
        if (resource->Status != RasterStatus::Ready)
        {
            return resource->Status == RasterStatus::Pending ? RasterStatus::NotReady : resource->Status;
        }
        if (kind == RasterBindingKind::UniformBuffer &&
            (resource->Buffer.Role != BufferRole::Uniform || limits.UniformOffsetAlignment == 0 ||
             entry->Offset % limits.UniformOffsetAlignment != 0 || entry->Size % 16 != 0 ||
             entry->Size < layout.Entries[i].MinSize || entry->Size > limits.MaxUniformRange ||
             !Fits(entry->Offset, entry->Size, resource->Buffer.Size)))
        {
            return RasterStatus::InvalidDescription;
        }
        set.Slots[i] = kind == RasterBindingKind::UniformBuffer ? RasterAccess::Slot(entry->Buffer)
                       : kind == RasterBindingKind::Texture2D   ? RasterAccess::Slot(entry->Texture)
                                                                : RasterAccess::Slot(entry->Sampler);
        set.Offsets[i] = entry->Offset;
        set.Sizes[i] = entry->Size;
    }
    return Publish(RasterKind::Set, output, [&](usize slot, Record& record) noexcept {
        record.Set = set;
        return backend::RasterCreateSet(slot, layout, set, record.Request);
    });
}
namespace
{
RasterStatus
ValidatePipeline(DeviceHandle device, const RasterPipelineDescription& description, RasterPipelineInfo& info) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto* vertex = Resolve(description.Vertex, RasterKind::Shader);
    const auto* fragment = Resolve(description.Fragment, RasterKind::Shader);
    const auto* layout = Resolve(description.Layout, RasterKind::Layout);
    if (vertex == nullptr || fragment == nullptr || layout == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    const Record* dependencies[]{vertex, fragment, layout};
    for (const auto* dependency : dependencies)
    {
        if (dependency->Status != RasterStatus::Ready)
        {
            return dependency->Status == RasterStatus::Pending ? RasterStatus::NotReady : dependency->Status;
        }
    }
    if (vertex->Shader.Stage != ShaderStage::Vertex || fragment->Shader.Stage != ShaderStage::Fragment ||
        !ShaderFits(vertex->Shader, layout->Layout) || !ShaderFits(fragment->Shader, layout->Layout) ||
        description.Streams.size() > 2 || description.Attributes.size() > 8 ||
        description.Attributes.size() != vertex->Shader.InputCount)
    {
        return RasterStatus::InvalidDescription;
    }
    info.Vertex = RasterAccess::Slot(description.Vertex);
    info.Fragment = RasterAccess::Slot(description.Fragment);
    info.Layout = RasterAccess::Slot(description.Layout);
    info.Depth = description.Depth;
    info.Blend = description.Blend;
    info.StreamCount = description.Streams.size();
    info.AttributeCount = description.Attributes.size();
    for (usize i = 0; i < info.StreamCount; ++i)
    {
        const auto& stream = description.Streams[i];
        if (stream.Stride == 0 || stream.Stride > 256 || stream.Stride % 4 != 0)
        {
            return RasterStatus::InvalidDescription;
        }
        info.Streams[i] = stream;
    }
    for (usize i = 0; i < info.AttributeCount; ++i)
    {
        const auto& attribute = description.Attributes[i];
        const auto format = static_cast<uint8>(attribute.Format);
        const usize size = (static_cast<usize>(format) + 2) * 4;
        if (format > 2 || attribute.Location >= 8 || attribute.Stream >= info.StreamCount ||
            attribute.Offset % 4 != 0 || !Fits(attribute.Offset, size, info.Streams[attribute.Stream].Stride))
        {
            return RasterStatus::InvalidDescription;
        }
        for (usize j = 0; j < i; ++j)
        {
            if (attribute.Location == info.Attributes[j].Location)
            {
                return RasterStatus::InvalidDescription;
            }
        }
        bool matched = false;
        for (usize j = 0; j < vertex->Shader.InputCount; ++j)
        {
            if (vertex->Shader.Inputs[j].Location == attribute.Location &&
                vertex->Shader.Inputs[j].Format == attribute.Format)
            {
                matched = true;
            }
        }
        if (!matched)
        {
            return RasterStatus::InvalidDescription;
        }
        info.Attributes[i] = attribute;
    }
    return RasterStatus::Ready;
}
} // namespace
RasterStatus CreateRasterPipeline(DeviceHandle device,
                                  const RasterPipelineDescription& description,
                                  RasterPipelineHandle& output) noexcept
{
    RasterPipelineInfo info;
    const auto valid = ValidatePipeline(device, description, info);
    if (valid != RasterStatus::Ready)
    {
        return valid;
    }
    return Publish(RasterKind::Pipeline, output, [&](usize slot, Record& record) noexcept {
        record.Pipeline = info;
        return backend::RasterCreatePipeline(slot, info, record.Request);
    });
}
namespace
{
RasterStatus ValidateDraw(DeviceHandle device, const RasterDraw& draw, RasterPacket& packet) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto* pipeline = Resolve(draw.Pipeline, RasterKind::Pipeline);
    const auto* set = Resolve(draw.Bindings, RasterKind::Set);
    const auto* indices = Resolve(draw.Indices, RasterKind::Buffer);
    if (pipeline == nullptr || set == nullptr || indices == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    const Record* dependencies[]{pipeline, set, indices};
    for (const auto* dependency : dependencies)
    {
        if (dependency->Status != RasterStatus::Ready)
        {
            return dependency->Status == RasterStatus::Pending ? RasterStatus::NotReady : dependency->Status;
        }
    }
    const auto& info = pipeline->Pipeline;
    if (set->Set.Layout != info.Layout || draw.Vertices.size() != info.StreamCount || draw.IndexCount == 0 ||
        draw.IndexCount % 3 != 0 || draw.VertexCount == 0 || draw.InstanceCount == 0 ||
        draw.InstanceCount > 0x7fffffffU ||
        (indices->Buffer.Role != BufferRole::Index16 && indices->Buffer.Role != BufferRole::Index32))
    {
        return RasterStatus::InvalidDescription;
    }
    const usize width = indices->Buffer.Role == BufferRole::Index16 ? 2 : 4;
    if (draw.IndexOffset % width != 0 || draw.IndexCount > ~usize{0} / width ||
        !Fits(draw.IndexOffset, static_cast<usize>(draw.IndexCount) * width, indices->Buffer.Size))
    {
        return RasterStatus::InvalidDescription;
    }
    for (usize i = 0; i < draw.IndexCount; ++i)
    {
        // Byte copies avoid alignment assumptions; index upload uses native-endian
        // unsigned integers, as do the selected native/browser graphics APIs.
        uint32 index = 0;
        if (width == 2)
        {
            uint16 value = 0;
            std::memcpy(&value, indices->Indices.GetData() + draw.IndexOffset + i * width, width);
            index = value;
        }
        else
        {
            std::memcpy(&index, indices->Indices.GetData() + draw.IndexOffset + i * width, width);
        }
        if (index >= draw.VertexCount || index == (width == 2 ? 0xffffU : 0xffffffffU))
        {
            return RasterStatus::InvalidDescription;
        }
    }
    packet.Pipeline = RasterAccess::Slot(draw.Pipeline);
    packet.Set = RasterAccess::Slot(draw.Bindings);
    packet.Indices = RasterAccess::Slot(draw.Indices);
    packet.IndexOffset = draw.IndexOffset;
    packet.IndexCount = draw.IndexCount;
    packet.InstanceCount = draw.InstanceCount;
    packet.Index32 = width == 4;
    for (usize i = 0; i < info.StreamCount; ++i)
    {
        const auto& slice = draw.Vertices[i];
        const auto* buffer = Resolve(slice.Buffer, RasterKind::Buffer);
        if (buffer == nullptr)
        {
            return RasterStatus::InvalidHandle;
        }
        if (buffer->Status != RasterStatus::Ready)
        {
            return buffer->Status == RasterStatus::Pending ? RasterStatus::NotReady : buffer->Status;
        }
        const usize count = info.Streams[i].PerInstance ? draw.InstanceCount : draw.VertexCount;
        if (buffer->Buffer.Role != BufferRole::Vertex || slice.Offset % 4 != 0 ||
            !Fits(slice.Offset, slice.Size, buffer->Buffer.Size) || count > slice.Size / info.Streams[i].Stride)
        {
            return RasterStatus::InvalidDescription;
        }
        packet.Vertices[i] = RasterAccess::Slot(slice.Buffer);
        packet.Offsets[i] = slice.Offset;
    }
    return RasterStatus::Ready;
}
RasterStatus ReserveFrameSubmission() noexcept
{
    if (gFrameSubmission != 0)
    {
        return RasterStatus::Ready;
    }
    if (gSubmissions.Next == 0)
    {
        return RasterStatus::IdentityExhausted;
    }
    const auto result = backend::RasterReserveSubmission();
    if (result == RasterStatus::Ready)
    {
        gFrameSubmission = gSubmissions.Next;
    }
    return result;
}
RasterStatus EncodePacket(const RasterPacket& packet) noexcept
{
    if (gDrawCount >= RASTER_DRAWS)
    {
        return RasterStatus::CapacityExceeded;
    }
    const auto reservation = ReserveFrameSubmission();
    if (reservation != RasterStatus::Ready)
    {
        return reservation;
    }
    if (!ClaimRasterFrame())
    {
        return RasterStatus::InvalidState;
    }
    FrameRetain(RasterKind::Pipeline, packet.Pipeline);
    FrameRetain(RasterKind::Set, packet.Set);
    FrameRetain(RasterKind::Buffer, packet.Indices);
    for (usize i = 0; i < At(RasterKind::Pipeline, packet.Pipeline).Pipeline.StreamCount; ++i)
    {
        FrameRetain(RasterKind::Buffer, packet.Vertices[i]);
    }
    ++gDrawCount;
    return backend::RasterDraw(packet);
}
} // namespace
RasterStatus DrawIndexed(DeviceHandle device, const RasterDraw& draw) noexcept
{
    if (!FrameOpen())
    {
        const auto admission = Admission(device, false);
        return admission == RasterStatus::Ready ? RasterStatus::InvalidState : admission;
    }
    RasterPacket packet;
    const auto valid = ValidateDraw(device, draw, packet);
    return valid == RasterStatus::Ready ? EncodePacket(packet) : valid;
}

namespace internal
{
void RasterExpect(uint32 request, RasterCallbacks callbacks) noexcept
{
    for (auto& records : gRecords)
    {
        for (auto& record : records)
        {
            if (record.Owner != 0 && record.Request == request)
            {
                record.Callbacks = static_cast<uint32>(callbacks);
                return;
            }
        }
    }
}
void RasterComplete(uint32 request, RasterStatus status) noexcept
{
    for (auto& records : gRecords)
    {
        for (auto& record : records)
        {
            if (record.Owner != 0 && record.Request == request && record.Callbacks != 0)
            {
                --record.Callbacks;
                if (status != RasterStatus::Ready)
                {
                    record.Status = status;
                }
                else if (record.Callbacks == 0 && record.Status == RasterStatus::Pending)
                {
                    record.Status = RasterStatus::Ready;
                }
                return;
            }
        }
    }
}
void RasterFail(uint32 request, RasterStatus status) noexcept
{
    for (auto& records : gRecords)
    {
        for (auto& record : records)
        {
            if (record.Owner != 0 && record.Request == request)
            {
                record.Status = status;
                return;
            }
        }
    }
}
void RasterBeginFrame() noexcept
{
    gFrameSubmission = 0;
    gDrawCount = 0;
    Collect();
}
void RasterEndFrame(bool accepted) noexcept
{
    if (gFrameSubmission != 0 && accepted)
    {
        (void)gSubmissions.Take();
        if (gFirstAcceptedSubmission == 0)
        {
            gFirstAcceptedSubmission = gFrameSubmission;
        }
        gLastAcceptedSubmission = gFrameSubmission;
        backend::RasterSubmit(gFrameSubmission);
    }
    if (!accepted)
    {
        backend::RasterDiscardSubmission();
    }
    for (auto& records : gRecords)
    {
        for (auto& record : records)
        {
            if (!record.InFrame)
            {
                continue;
            }
            if (accepted)
            {
                record.LastUse = gFrameSubmission;
            }
            record.InFrame = false;
            --record.References;
        }
    }
    gFrameSubmission = 0;
    Collect();
}
void ReleaseRasterResources() noexcept
{
    // Shutdown owns all physical release. Native reset waits or drops the lost
    // device; browser callbacks retain their stable bookkeeping until delivery.
    backend::RasterReset();
    backend::LifetimeReset();
    ResetLifetimeRecords();
    for (usize kind = KINDS; kind-- > 0;)
    {
        for (usize slot = 0; slot < RASTER_CAPACITY; ++slot)
        {
            auto& record = gRecords[kind][slot];
            if (record.Owner == 0)
            {
                continue;
            }
            backend::RasterDestroy(static_cast<RasterKind>(kind), slot);
            const auto generation = RasterNextGeneration(record.Generation);
            record = {};
            record.Generation = generation;
        }
    }
    backend::RasterShutdown();
    gFirstAcceptedSubmission = 0;
    gLastAcceptedSubmission = 0;
    gFrameSubmission = 0;
    gDrawCount = 0;
}
} // namespace internal
RasterStatus GetStatus(DeviceHandle device, BufferHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Buffer);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, BufferHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Buffer);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, TextureHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Texture);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, TextureHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Texture);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, TextureViewHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::View);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, TextureViewHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::View);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, SamplerHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Sampler);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, SamplerHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Sampler);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, RasterShaderHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Shader);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, RasterShaderHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Shader);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, BindingLayoutHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Layout);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, BindingLayoutHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Layout);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, BindingSetHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Set);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, BindingSetHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Set);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus GetStatus(DeviceHandle device, RasterPipelineHandle handle) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    Collect();
    const auto current = Admission(device, false);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    const auto* record = Resolve(handle, RasterKind::Pipeline);
    return record == nullptr ? RasterStatus::InvalidHandle : record->Status;
}
RasterStatus Destroy(DeviceHandle device, RasterPipelineHandle& handle) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    auto* record = Resolve(handle, RasterKind::Pipeline);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (record->ServiceOwned)
    {
        return RasterStatus::InvalidState;
    }
    record->Published = false;
    --record->References;
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
} // namespace ludus::graphics::rhi

#include "internal/lifetime_services.h"
