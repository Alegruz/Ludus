#pragma once
// Included only by raster.cpp; graph plans and direct draws share its registry.
// Thanks to the Ludus RHI/GDI architecture, "Pass declarations and deterministic
// compilation" (docs/architecture/rhi-gdi.md), for authored order, versioned
// contents, observable history roots and completion-gated object pooling.
namespace ludus::graphics::rhi
{
namespace internal
{
struct GraphVersionIndex final
{
    usize Resource;
    uint32 Version;
};
struct GraphAccess final
{
    static uint64 Owner(OrderedGraph h) noexcept
    {
        return h.Owner;
    }
    static uint64 Generation(OrderedGraph h) noexcept
    {
        return h.Generation;
    }
    static usize Slot(OrderedGraph h) noexcept
    {
        return h.Slot == 0 ? 4 : h.Slot - 1;
    }
    static void Set(OrderedGraph& h, const RasterIdentity& identity) noexcept
    {
        h.Owner = DeviceOwner();
        h.Generation = identity.Generation;
        h.Slot = static_cast<uint32>(identity.Slot + 1);
    }
    static bool Belongs(GraphVersion v, OrderedGraph h) noexcept
    {
        return v.Owner != 0 && v.Owner == h.Owner && v.Generation == h.Generation && v.Graph == h.Slot;
    }
    static usize Resource(GraphVersion v) noexcept
    {
        return v.Resource == 0 ? 16 : v.Resource - 1;
    }
    static uint32 Version(GraphVersion v) noexcept
    {
        return v.Version;
    }
    static void Set(GraphVersion& v, OrderedGraph h, const GraphVersionIndex& index) noexcept
    {
        v.Owner = h.Owner;
        v.Generation = h.Generation;
        v.Graph = h.Slot;
        v.Resource = static_cast<uint32>(index.Resource + 1);
        v.Version = index.Version;
    }
    static bool Null(GraphVersion v) noexcept
    {
        return v.Owner == 0;
    }
};
} // namespace internal
namespace
{
constexpr usize GRAPH_COUNT = 4;
constexpr usize GRAPH_RESOURCES = 16;
constexpr usize GRAPH_PASSES = 32;
constexpr usize GRAPH_DEPENDENCIES = 512;
constexpr usize GRAPH_POOL = 8;
struct GraphResourceRecord final
{
    RasterKind Kind = RasterKind::Buffer;
    usize Slot = 0;
    uint32 Versions = 0;
    uint32 RootVersion = 0;
    bool Root = false;
    bool Transient = false;
    RasterTextureState Import;
    uint64 BufferRevision = 0;
    RasterTextureUse Final = RasterTextureUse::SampledBoth;
};
struct GraphPassRecord final
{
    RasterPassInfo Info;
    GraphUse Uses[GRAPH_RESOURCES]{};
    usize UseCount = 0;
    usize FirstDraw = 0;
    usize DrawCount = 0;
    bool SideEffect = false;
    bool IsCompute = false;
    ComputePacket Dispatch;
};
struct GraphRecord final : LifetimeRecord
{
    bool Compiled = false;
    usize ResourceCount = 0;
    usize PassCount = 0;
    usize DrawCount = 0;
    GraphResourceRecord Resources[GRAPH_RESOURCES]{};
    GraphPassRecord Passes[GRAPH_PASSES]{};
    RasterPacket Draws[LIFETIME_BATCH_DRAWS]{};
    GraphReport Report;
};
struct GraphPoolRecord final
{
    bool Occupied = false;
    usize Texture = 0;
};
GraphRecord gGraphs[GRAPH_COUNT];
GraphPoolRecord gGraphPool[GRAPH_POOL];
GraphRecord* ResolveGraph(OrderedGraph handle) noexcept
{
    const auto slot = GraphAccess::Slot(handle);
    if (slot >= GRAPH_COUNT)
    {
        return nullptr;
    }
    auto& graph = gGraphs[slot];
    return graph.Published && graph.Owner == DeviceOwner() && graph.Owner == GraphAccess::Owner(handle) &&
                   graph.Generation == GraphAccess::Generation(handle)
               ? &graph
               : nullptr;
}
RasterStatus GraphAdmission(DeviceHandle device, OrderedGraph handle, GraphRecord*& graph, bool setup = true) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    graph = ResolveGraph(handle);
    if (graph == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    return setup && graph->Compiled ? RasterStatus::InvalidState : RasterStatus::Ready;
}
bool ValidVersion(const GraphRecord& graph, OrderedGraph handle, GraphVersion version) noexcept
{
    const auto resource = GraphAccess::Resource(version);
    return GraphAccess::Belongs(version, handle) && resource < graph.ResourceCount &&
           GraphAccess::Version(version) <= graph.Resources[resource].Versions;
}
RasterTextureState TextureState(const Record& record) noexcept
{
    RasterTextureState result;
    result.Revision = record.TextureRevision;
    if (record.LastUse != 0)
    {
        LifetimeAccess::Set(result.Dependency, record.LastUse);
    }
    result.Use = record.TextureUse;
    result.ColorDefined = record.ColorDefined;
    result.DepthDefined = record.DepthDefined;
    return result;
}
bool SameTextureState(const RasterTextureState& a, const RasterTextureState& b) noexcept
{
    return a.Revision == b.Revision && a.Use == b.Use && a.ColorDefined == b.ColorDefined &&
           a.DepthDefined == b.DepthDefined &&
           LifetimeAccess::Owner(a.Dependency) == LifetimeAccess::Owner(b.Dependency) &&
           LifetimeAccess::Ordinal(a.Dependency) == LifetimeAccess::Ordinal(b.Dependency);
}
bool SameTextureDescription(const TextureDescription& a, const TextureDescription& b) noexcept
{
    return a.Width == b.Width && a.Height == b.Height && a.Format == b.Format && a.Attachment == b.Attachment;
}
void ResetGraph(GraphRecord& graph) noexcept
{
    // Reset bounded members in place: a whole GraphRecord temporary exceeds the
    // default browser stack and can corrupt adjacent allocator bookkeeping.
    const auto generation = RasterNextGeneration(graph.Generation);
    static_cast<LifetimeRecord&>(graph) = {};
    graph.Generation = generation;
    graph.Compiled = false;
    graph.ResourceCount = graph.PassCount = graph.DrawCount = 0;
    for (auto& resource : graph.Resources)
    {
        resource = {};
    }
    for (auto& pass : graph.Passes)
    {
        pass = {};
    }
    for (auto& draw : graph.Draws)
    {
        draw = {};
    }
    graph.Report = {};
}
void DropGraph(GraphRecord& graph) noexcept
{
    for (usize i = 0; i < graph.DrawCount; ++i)
    {
        PacketReferences(graph.Draws[i], false);
    }
    for (usize p = 0; p < graph.PassCount; ++p)
    {
        if (graph.Passes[p].IsCompute)
        {
            ComputeReferences(graph.Passes[p].Dispatch, false);
        }
    }
    for (usize i = 0; i < graph.ResourceCount; ++i)
    {
        const auto& resource = graph.Resources[i];
        if (resource.Transient)
        {
            auto& texture = At(RasterKind::Texture, resource.Slot);
            texture.Published = false;
            texture.Generation = RasterNextGeneration(texture.Generation);
        }
        Release(resource.Kind, resource.Slot);
    }
    ResetGraph(graph);
}
void ResetGraphRecords() noexcept
{
    // Teardown releases every registry record unconditionally after backend drain.
    for (auto& graph : gGraphs)
    {
        ResetGraph(graph);
    }
    for (auto& pool : gGraphPool)
    {
        pool = {};
    }
}
RasterTarget PassTarget(const RasterPassInfo& pass) noexcept
{
    if (pass.Texture == RASTER_CAPACITY)
    {
        return RasterTarget::Surface;
    }
    return At(RasterKind::Texture, pass.Texture).Texture.Format == RasterFormat::Rgba8Unorm ? RasterTarget::Rgba8Unorm
                                                                                            : RasterTarget::Rgba8Srgb;
}
RasterStatus ValidatePassDescription(const RasterPassDescription& description, RasterPassInfo& pass) noexcept
{
    if (static_cast<uint8>(description.ColorLoad) > 2 || static_cast<uint8>(description.DepthLoad) > 2 ||
        static_cast<uint8>(description.ColorStore) > 1 || static_cast<uint8>(description.DepthStore) > 1 ||
        (description.DepthReadOnly && description.DepthLoad != RasterLoad::Load))
    {
        return RasterStatus::InvalidDescription;
    }
    for (const auto component : description.Clear)
    {
        const bool validClear = component >= 0 && component <= 1;
        if (!validClear)
        {
            return RasterStatus::InvalidDescription;
        }
    }
    pass.Description = description;
    if (RasterAccess::Owner(description.Color) == 0)
    {
        return RasterStatus::Ready;
    }
    const auto* texture = Resolve(description.Color, RasterKind::Texture);
    if (texture == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (!texture->Texture.Attachment)
    {
        return RasterStatus::InvalidDescription;
    }
    if (texture->Status != RasterStatus::Ready)
    {
        return texture->Status == RasterStatus::Pending ? RasterStatus::NotReady : texture->Status;
    }
    if (texture->TextureRevision == ~uint64{0})
    {
        return RasterStatus::IdentityExhausted;
    }
    pass.Texture = RasterAccess::Slot(description.Color);
    return RasterStatus::Ready;
}
RasterStatus ValidatePacketPass(const RasterPacket& packet, const RasterPassInfo& pass, bool contents) noexcept
{
    const auto& pipeline = At(RasterKind::Pipeline, packet.Pipeline).Pipeline;
    if (pipeline.Target != PassTarget(pass) ||
        (pass.Description.DepthReadOnly && pipeline.Depth && pipeline.DepthWrite))
    {
        return RasterStatus::InvalidDescription;
    }
    const auto& set = At(RasterKind::Set, packet.Set).Set;
    const auto& layout = At(RasterKind::Layout, set.Layout).Layout;
    for (usize i = 0; i < layout.Count; ++i)
    {
        if (layout.Entries[i].Kind != RasterBindingKind::Texture2D)
        {
            continue;
        }
        const auto slot = At(RasterKind::View, set.Slots[i]).TextureSlot;
        const auto& texture = At(RasterKind::Texture, slot);
        if (slot == pass.Texture)
        {
            return RasterStatus::InvalidDescription;
        }
        if (texture.Status != RasterStatus::Ready)
        {
            return texture.Status;
        }
        if (texture.ServiceOwned && !texture.Published)
        {
            return RasterStatus::InvalidHandle;
        }
        if (texture.TextureRevision == ~uint64{0})
        {
            return RasterStatus::IdentityExhausted;
        }
        if (contents && !(texture.InFrame ? texture.FrameColorDefined : texture.ColorDefined))
        {
            return RasterStatus::InvalidDescription;
        }
        if (contents)
        {
            const auto use = texture.InFrame ? texture.FrameTextureUse : texture.TextureUse;
            const auto visibility = static_cast<uint8>(layout.Entries[i].Visibility);
            const uint8 available = use == RasterTextureUse::SampledVertex     ? 1
                                    : use == RasterTextureUse::SampledFragment ? 2
                                    : use == RasterTextureUse::SampledBoth     ? 3
                                                                               : 0;
            if ((available & visibility) != visibility)
            {
                return RasterStatus::InvalidState;
            }
        }
    }
    return RasterStatus::Ready;
}
void RetainPacketTextures(const RasterPacket& packet) noexcept
{
    const auto& set = At(RasterKind::Set, packet.Set).Set;
    const auto& layout = At(RasterKind::Layout, set.Layout).Layout;
    for (usize i = 0; i < layout.Count; ++i)
    {
        if (layout.Entries[i].Kind != RasterBindingKind::Texture2D)
        {
            continue;
        }
        const auto slot = At(RasterKind::View, set.Slots[i]).TextureSlot;
        FrameRetain(RasterKind::Texture, slot);
        auto& texture = At(RasterKind::Texture, slot);
        const auto visibility = static_cast<uint8>(layout.Entries[i].Visibility);
        const auto use = visibility == 1   ? RasterTextureUse::SampledVertex
                         : visibility == 2 ? RasterTextureUse::SampledFragment
                                           : RasterTextureUse::SampledBoth;
        const bool alreadySampled = texture.FrameTextureUse >= RasterTextureUse::SampledVertex &&
                                    texture.FrameTextureUse <= RasterTextureUse::SampledBoth;
        texture.FrameTextureUse =
            alreadySampled && texture.FrameTextureUse != use ? RasterTextureUse::SampledBoth : use;
    }
}
void ApplyPassState(const RasterPassInfo& pass) noexcept
{
    const auto& description = pass.Description;
    bool* color = &gSurfaceColorDefined;
    bool* depth = &gSurfaceDepthDefined;
    if (pass.Texture != RASTER_CAPACITY)
    {
        FrameRetain(RasterKind::Texture, pass.Texture);
        auto& texture = At(RasterKind::Texture, pass.Texture);
        texture.FrameTextureUse = RasterTextureUse::ColorAttachment;
        color = &texture.FrameColorDefined;
        depth = &texture.FrameDepthDefined;
    }
    *color = description.ColorStore == RasterStore::Store &&
             (description.ColorLoad == RasterLoad::Clear || (description.ColorLoad == RasterLoad::Load && *color));
    *depth = description.DepthStore == RasterStore::Store &&
             (description.DepthLoad == RasterLoad::Clear || (description.DepthLoad == RasterLoad::Load && *depth));
    gRasterPass = pass;
}
RasterStatus StartPass(const RasterPassInfo& pass) noexcept
{
    backend::RasterEndPass();
    if (GetStartup().State != StartupState::Ready)
    {
        return GetStartup().State == StartupState::DeviceLost ? RasterStatus::DeviceLost : RasterStatus::Failed;
    }
    if (gRasterPass.Texture != RASTER_CAPACITY && gRasterPass.Texture != pass.Texture)
    {
        backend::RasterTextureBarrier(gRasterPass.Texture, RasterTextureUse::SampledBoth);
        if (GetStartup().State == StartupState::Ready)
        {
            At(RasterKind::Texture, gRasterPass.Texture).FrameTextureUse = RasterTextureUse::SampledBoth;
        }
    }
    if (GetStartup().State != StartupState::Ready)
    {
        return GetStartup().State == StartupState::DeviceLost ? RasterStatus::DeviceLost : RasterStatus::Failed;
    }
    const auto started = backend::RasterBeginPass(pass);
    const auto current = GetStartup().State;
    if (current != StartupState::Ready)
    {
        return current == StartupState::DeviceLost ? RasterStatus::DeviceLost : RasterStatus::Failed;
    }
    if (started == RasterStatus::Ready)
    {
        ApplyPassState(pass);
    }
    return started;
}
bool CopyGraphText(const char* input, char* output, usize capacity, bool required) noexcept
{
    if (input == nullptr)
    {
        return !required;
    }
    usize length = 0;
    while (length < capacity && input[length] != '\0')
    {
        ++length;
    }
    if (length == capacity || (required && length == 0))
    {
        return false;
    }
    std::memcpy(output, input, length);
    return true;
}
RasterStatus AddImport(GraphRecord& graph,
                       OrderedGraph handle,
                       RasterKind kind,
                       usize slot,
                       GraphVersion& output,
                       bool transient = false) noexcept
{
    if (!GraphAccess::Null(output))
    {
        return RasterStatus::InvalidState;
    }
    if (graph.ResourceCount == GRAPH_RESOURCES)
    {
        return RasterStatus::CapacityExceeded;
    }
    for (usize i = 0; i < graph.ResourceCount; ++i)
    {
        if (graph.Resources[i].Kind == kind && graph.Resources[i].Slot == slot)
        {
            return RasterStatus::InvalidDescription;
        }
    }
    const auto index = graph.ResourceCount++;
    auto& resource = graph.Resources[index];
    resource.Kind = kind;
    resource.Slot = slot;
    resource.Transient = transient;
    if (kind == RasterKind::Texture)
    {
        resource.Import = TextureState(At(kind, slot));
    }
    Retain(kind, slot);
    GraphAccess::Set(output, handle, {index, 0});
    return RasterStatus::Ready;
}
} // namespace
namespace internal
{
bool RasterFinishFrame() noexcept
{
    if (!gSurfaceColorDefined)
    {
        internal::FaultRasterSession();
        return false;
    }
    backend::RasterEndPass();
    if (GetStartup().State != StartupState::Ready)
    {
        return false;
    }
    if (gGraphExecuting)
    {
        return true;
    }
    for (usize slot = 0; slot < RASTER_CAPACITY; ++slot)
    {
        auto& texture = At(RasterKind::Texture, slot);
        if (texture.InFrame && texture.Texture.Attachment &&
            texture.FrameTextureUse == RasterTextureUse::ColorAttachment)
        {
            backend::RasterTextureBarrier(slot, RasterTextureUse::SampledBoth);
            if (GetStartup().State != StartupState::Ready)
            {
                return false;
            }
            texture.FrameTextureUse = RasterTextureUse::SampledBoth;
        }
    }
    return true;
}
} // namespace internal
RasterStatus GetTextureState(DeviceHandle device, TextureHandle texture, RasterTextureState& output) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto* record = Resolve(texture, RasterKind::Texture);
    if (record == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (record->Status != RasterStatus::Ready)
    {
        return record->Status == RasterStatus::Pending ? RasterStatus::NotReady : record->Status;
    }
    output = TextureState(*record);
    return RasterStatus::Ready;
}
RasterStatus BeginRasterPass(DeviceHandle device, const RasterPassDescription& description) noexcept
{
    const auto admission = Admission(device, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    if (!FrameOpen())
    {
        return RasterStatus::InvalidState;
    }
    RasterPassInfo pass;
    const auto valid = ValidatePassDescription(description, pass);
    if (valid != RasterStatus::Ready)
    {
        return valid;
    }
    bool color = gSurfaceColorDefined;
    bool depth = gSurfaceDepthDefined;
    if (pass.Texture != RASTER_CAPACITY)
    {
        const auto& texture = At(RasterKind::Texture, pass.Texture);
        color = texture.InFrame ? texture.FrameColorDefined : texture.ColorDefined;
        depth = texture.InFrame ? texture.FrameDepthDefined : texture.DepthDefined;
    }
    if ((description.ColorLoad == RasterLoad::Load && !color) || (description.DepthLoad == RasterLoad::Load && !depth))
    {
        return RasterStatus::InvalidDescription;
    }
    const auto prepared = backend::RasterPreparePass(pass);
    if (prepared != RasterStatus::Ready)
    {
        return prepared;
    }
    const auto reserved = ReserveFrameSubmission();
    if (reserved != RasterStatus::Ready)
    {
        return reserved;
    }
    if (!ClaimRasterFrame())
    {
        return RasterStatus::InvalidState;
    }
    const auto started = StartPass(pass);
    if (started != RasterStatus::Ready)
    {
        internal::FaultRasterSession();
    }
    return started;
}
RasterStatus CreateOrderedGraph(DeviceHandle device, OrderedGraph& output) noexcept
{
    const auto admission = Admission(device);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    if (GraphAccess::Owner(output) != 0)
    {
        return RasterStatus::InvalidState;
    }
    usize slot = 0;
    const auto reserved = ReserveLifetime(gGraphs, slot);
    if (reserved != RasterStatus::Ready)
    {
        return reserved;
    }
    auto& graph = gGraphs[slot];
    graph.Owner = DeviceOwner();
    graph.Published = true;
    GraphAccess::Set(output, {DeviceOwner(), slot, graph.Generation});
    return RasterStatus::Ready;
}
RasterStatus
ImportGraphBuffer(DeviceHandle device, OrderedGraph handle, BufferHandle buffer, GraphVersion& output) noexcept
{
    GraphRecord* graph = nullptr;
    const auto admission = GraphAdmission(device, handle, graph);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto* source = Resolve(buffer, RasterKind::Buffer);
    if (source == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (source->Status != RasterStatus::Ready)
    {
        return source->Status == RasterStatus::Pending ? RasterStatus::NotReady : source->Status;
    }
    const auto added = AddImport(*graph, handle, RasterKind::Buffer, RasterAccess::Slot(buffer), output);
    if (added == RasterStatus::Ready)
    {
        graph->Resources[GraphAccess::Resource(output)].BufferRevision = source->BufferRevision;
    }
    return added;
}
RasterStatus ImportGraphTexture(DeviceHandle device,
                                OrderedGraph handle,
                                TextureHandle texture,
                                const RasterTextureState& state,
                                RasterTextureUse finalUse,
                                GraphVersion& output) noexcept
{
    GraphRecord* graph = nullptr;
    const auto admission = GraphAdmission(device, handle, graph);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    const auto* source = Resolve(texture, RasterKind::Texture);
    if (source == nullptr)
    {
        return RasterStatus::InvalidHandle;
    }
    if (source->ServiceOwned)
    {
        return RasterStatus::InvalidState;
    }
    if (source->Status != RasterStatus::Ready)
    {
        return source->Status == RasterStatus::Pending ? RasterStatus::NotReady : source->Status;
    }
    if (finalUse < RasterTextureUse::SampledVertex || finalUse > RasterTextureUse::ColorAttachment ||
        (finalUse == RasterTextureUse::ColorAttachment && !source->Texture.Attachment))
    {
        return RasterStatus::InvalidDescription;
    }
    if (!SameTextureState(state, TextureState(*source)))
    {
        return RasterStatus::InvalidState;
    }
    const auto added = AddImport(*graph, handle, RasterKind::Texture, RasterAccess::Slot(texture), output);
    if (added == RasterStatus::Ready)
    {
        graph->Resources[GraphAccess::Resource(output)].Final = finalUse;
    }
    return added;
}
RasterStatus CreateGraphTexture(DeviceHandle device,
                                OrderedGraph handle,
                                const TextureDescription& description,
                                GraphVersion& version,
                                TextureHandle& output) noexcept
{
    GraphRecord* graph = nullptr;
    const auto admission = GraphAdmission(device, handle, graph);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    if (!GraphAccess::Null(version) || RasterAccess::Owner(output) != 0)
    {
        return RasterStatus::InvalidState;
    }
    if (graph->ResourceCount == GRAPH_RESOURCES)
    {
        return RasterStatus::CapacityExceeded;
    }
    if (!description.Attachment)
    {
        return RasterStatus::InvalidDescription;
    }
    const auto completed = backend::RasterCompleted();
    const auto current = Admission(device);
    if (current != RasterStatus::Ready)
    {
        return current;
    }
    usize freePool = GRAPH_POOL, evictPool = GRAPH_POOL;
    for (usize i = 0; i < GRAPH_POOL; ++i)
    {
        const auto& pool = gGraphPool[i];
        if (!pool.Occupied)
        {
            if (freePool == GRAPH_POOL)
            {
                freePool = i;
            }
            continue;
        }
        auto& texture = At(RasterKind::Texture, pool.Texture);
        const bool idle = texture.References == 1 && texture.LastUse <= completed && texture.Callbacks == 0 &&
                          texture.Status != RasterStatus::Pending;
        if (idle && evictPool == GRAPH_POOL)
        {
            evictPool = i;
        }
        if (idle && texture.Status == RasterStatus::Ready && texture.Generation != 0 &&
            SameTextureDescription(texture.Texture, description))
        {
            texture.Published = true;
            texture.ColorDefined = texture.DepthDefined = false;
            RasterAccess::Set(output, {DeviceOwner(), pool.Texture, texture.Generation});
            return AddImport(*graph, handle, RasterKind::Texture, pool.Texture, version, true);
        }
    }
    if (freePool == GRAPH_POOL && evictPool != GRAPH_POOL)
    {
        freePool = evictPool;
        Release(RasterKind::Texture, gGraphPool[freePool].Texture);
        gGraphPool[freePool] = {};
        Collect();
    }
    if (freePool == GRAPH_POOL)
    {
        return RasterStatus::CapacityExceeded;
    }
    const auto created = CreateTexture(device, description, {}, output);
    if (created != RasterStatus::Ready && created != RasterStatus::Pending)
    {
        return created;
    }
    const auto slot = RasterAccess::Slot(output);
    At(RasterKind::Texture, slot).ServiceOwned = true;
    gGraphPool[freePool] = {true, slot};
    (void)AddImport(*graph, handle, RasterKind::Texture, slot, version, true);
    return created;
}
RasterStatus
NextGraphVersion(DeviceHandle device, OrderedGraph handle, GraphVersion previous, GraphVersion& output) noexcept
{
    GraphRecord* graph = nullptr;
    const auto admission = GraphAdmission(device, handle, graph);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    if (!ValidVersion(*graph, handle, previous))
    {
        return RasterStatus::InvalidHandle;
    }
    if (!GraphAccess::Null(output))
    {
        return RasterStatus::InvalidState;
    }
    auto& resource = graph->Resources[GraphAccess::Resource(previous)];
    const auto& physical = At(resource.Kind, resource.Slot);
    const bool mutableResource =
        resource.Kind == RasterKind::Texture ? physical.Texture.Attachment : StorageRole(physical.Buffer.Role);
    if (!mutableResource || GraphAccess::Version(previous) != resource.Versions)
    {
        return RasterStatus::InvalidDescription;
    }
    if (resource.Versions == GRAPH_PASSES)
    {
        return RasterStatus::CapacityExceeded;
    }
    GraphAccess::Set(output, handle, {GraphAccess::Resource(previous), ++resource.Versions});
    return RasterStatus::Ready;
}
RasterStatus AddGraphPass(DeviceHandle device, OrderedGraph handle, const GraphPassDescription& description) noexcept
{
    GraphRecord* graph = nullptr;
    const auto admission = GraphAdmission(device, handle, graph);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    if (graph->PassCount == GRAPH_PASSES || description.DrawCount > LIFETIME_BATCH_DRAWS - graph->DrawCount ||
        description.UseCount > GRAPH_RESOURCES)
    {
        return RasterStatus::CapacityExceeded;
    }
    if ((description.DrawCount != 0 && description.Draws == nullptr) ||
        (description.UseCount != 0 && description.Uses == nullptr))
    {
        return RasterStatus::InvalidDescription;
    }
    GraphPassRecord pass;
    char name[64]{};
    char source[128]{};
    if (!CopyGraphText(description.Name, name, sizeof(name), true) ||
        !CopyGraphText(description.Source, source, sizeof(source), false))
    {
        return RasterStatus::InvalidDescription;
    }
    pass.IsCompute = description.Dispatch != nullptr;
    if (pass.IsCompute)
    {
        if (description.DrawCount != 0 || RasterAccess::Owner(description.Attachment.Color) != 0)
        {
            return RasterStatus::InvalidDescription;
        }
        const auto valid = ValidateCompute(device, *description.Dispatch, pass.Dispatch);
        if (valid != RasterStatus::Ready)
        {
            return valid;
        }
    }
    else
    {
        const auto valid = ValidatePassDescription(description.Attachment, pass.Info);
        if (valid != RasterStatus::Ready)
        {
            return valid;
        }
    }
    for (usize i = 0; i < description.UseCount; ++i)
    {
        const auto& use = description.Uses[i];
        if (!ValidVersion(*graph, handle, use.Resource))
        {
            return RasterStatus::InvalidHandle;
        }
        if (static_cast<uint8>(use.Access) > 8 || static_cast<uint8>(use.Visibility) < 1 ||
            static_cast<uint8>(use.Visibility) > 4)
        {
            return RasterStatus::InvalidDescription;
        }
        pass.Uses[i] = use;
    }
    RasterPacket packets[LIFETIME_BATCH_DRAWS]{};
    for (usize i = 0; i < description.DrawCount; ++i)
    {
        const auto draw = ValidateDraw(device, description.Draws[i], packets[i]);
        if (draw != RasterStatus::Ready)
        {
            return draw;
        }
    }
    pass.UseCount = description.UseCount;
    pass.FirstDraw = graph->DrawCount;
    pass.DrawCount = description.DrawCount;
    pass.SideEffect = description.SideEffect;
    const auto index = graph->PassCount++;
    graph->Passes[index] = pass;
    if (pass.IsCompute)
    {
        ComputeReferences(pass.Dispatch, true);
    }
    std::memcpy(graph->Report.Names[index], name, sizeof(name));
    std::memcpy(graph->Report.Sources[index], source, sizeof(source));
    graph->Report.Lines[index] = description.Line;
    for (usize i = 0; i < description.DrawCount; ++i)
    {
        PacketReferences(packets[i], true);
        graph->Draws[graph->DrawCount++] = packets[i];
    }
    return RasterStatus::Ready;
}
RasterStatus AddGraphRoot(DeviceHandle device, OrderedGraph handle, GraphVersion version, GraphRoot root) noexcept
{
    GraphRecord* graph = nullptr;
    const auto admission = GraphAdmission(device, handle, graph);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    if (!ValidVersion(*graph, handle, version))
    {
        return RasterStatus::InvalidHandle;
    }
    if (static_cast<uint8>(root) > 2)
    {
        return RasterStatus::InvalidDescription;
    }
    auto& resource = graph->Resources[GraphAccess::Resource(version)];
    if (resource.Transient)
    {
        return RasterStatus::InvalidDescription;
    }
    if (resource.Root && resource.RootVersion != GraphAccess::Version(version))
    {
        return RasterStatus::InvalidState;
    }
    resource.Root = true;
    resource.RootVersion = GraphAccess::Version(version);
    return RasterStatus::Ready;
}
namespace
{
bool IsWrite(GraphAccessMode access) noexcept
{
    return access == GraphAccessMode::ColorWrite || access == GraphAccessMode::ColorReadWrite ||
           access == GraphAccessMode::StorageReadWrite;
}
const GraphUse* FindUse(const GraphRecord& graph,
                        const GraphPassRecord& pass,
                        RasterKind kind,
                        usize slot,
                        GraphAccessMode access,
                        usize offset = 0,
                        usize size = 0,
                        uint8 visibility = 0) noexcept
{
    for (usize i = 0; i < pass.UseCount; ++i)
    {
        const auto& use = pass.Uses[i];
        const auto& resource = graph.Resources[GraphAccess::Resource(use.Resource)];
        if (resource.Kind == kind && resource.Slot == slot && use.Access == access && offset >= use.Offset &&
            Fits(offset - use.Offset, size, use.Size) &&
            (static_cast<uint8>(use.Visibility) & visibility) == visibility)
        {
            return &use;
        }
    }
    return nullptr;
}
RasterStatus
CheckDeclaredPacket(const GraphRecord& graph, const GraphPassRecord& pass, const RasterPacket& packet) noexcept
{
    const auto valid = ValidatePacketPass(packet, pass.Info, false);
    if (valid != RasterStatus::Ready)
    {
        return valid;
    }
    const auto& pipeline = At(RasterKind::Pipeline, packet.Pipeline).Pipeline;
    for (usize i = 0; i < pipeline.StreamCount; ++i)
    {
        if (FindUse(graph,
                    pass,
                    RasterKind::Buffer,
                    packet.Vertices[i],
                    GraphAccessMode::Vertex,
                    packet.Offsets[i],
                    packet.Sizes[i]) == nullptr)
        {
            return RasterStatus::InvalidDescription;
        }
    }
    const auto indexSize = static_cast<usize>(packet.IndexCount) * (packet.Index32 ? 4 : 2);
    if (FindUse(graph,
                pass,
                RasterKind::Buffer,
                packet.Indices,
                GraphAccessMode::Index,
                packet.IndexOffset,
                indexSize) == nullptr)
    {
        return RasterStatus::InvalidDescription;
    }
    if (packet.Indirect != RASTER_CAPACITY && FindUse(graph,
                                                      pass,
                                                      RasterKind::Buffer,
                                                      packet.Indirect,
                                                      GraphAccessMode::Indirect,
                                                      packet.IndirectOffset,
                                                      sizeof(IndexedIndirectArguments)) == nullptr)
    {
        return RasterStatus::InvalidDescription;
    }
    const auto& set = At(RasterKind::Set, packet.Set).Set;
    const auto& layout = At(RasterKind::Layout, set.Layout).Layout;
    for (usize i = 0; i < layout.Count; ++i)
    {
        const auto& binding = layout.Entries[i];
        if (binding.Kind == RasterBindingKind::Sampler)
        {
            continue;
        }
        const bool buffer = binding.Kind == RasterBindingKind::UniformBuffer;
        const auto slot = buffer ? set.Slots[i] : At(RasterKind::View, set.Slots[i]).TextureSlot;
        if (FindUse(graph,
                    pass,
                    buffer ? RasterKind::Buffer : RasterKind::Texture,
                    slot,
                    buffer ? GraphAccessMode::Uniform : GraphAccessMode::Sampled,
                    buffer ? set.Offsets[i] : 0,
                    buffer ? set.Sizes[i] : 0,
                    static_cast<uint8>(binding.Visibility)) == nullptr)
        {
            return RasterStatus::InvalidDescription;
        }
    }
    return RasterStatus::Ready;
}
struct GraphTrack final
{
    uint32 Version = 0;
    uint32 Writer = 32;
    uint32 Readers = 0;
    bool Defined = false;
    bool DepthDefined = false;
    GraphAccessMode LastWrite = GraphAccessMode::ColorWrite;
    GraphAccessMode ReadAccess[GRAPH_PASSES]{};
};
bool AddDependency(GraphReport& report,
                   usize before,
                   usize after,
                   usize resource,
                   GraphHazard hazard,
                   GraphAccessMode source,
                   GraphAccessMode destination) noexcept
{
    if (report.DependencyCount == GRAPH_DEPENDENCIES)
    {
        return false;
    }
    report.Dependencies[report.DependencyCount++] = {static_cast<uint32>(before),
                                                     static_cast<uint32>(after),
                                                     static_cast<uint32>(resource),
                                                     hazard,
                                                     source,
                                                     destination};
    return true;
}
RasterStatus CompileGraph(GraphRecord& graph, GraphReport& report, bool reference) noexcept
{
    GraphTrack tracks[GRAPH_RESOURCES]{};
    bool live[GRAPH_PASSES]{};
    bool surfaceColor = true, surfaceDepth = false;
    bool imported[GRAPH_RESOURCES]{};
    for (usize r = 0; r < graph.ResourceCount; ++r)
    {
        const auto& resource = graph.Resources[r];
        const auto& physical = At(resource.Kind, resource.Slot);
        if (physical.Status != RasterStatus::Ready)
        {
            report.ErrorResource = static_cast<uint32>(r);
            return physical.Status;
        }
        if (resource.Kind == RasterKind::Texture && !SameTextureState(resource.Import, TextureState(physical)))
        {
            report.ErrorResource = static_cast<uint32>(r);
            return RasterStatus::InvalidState;
        }
        if (resource.Kind == RasterKind::Buffer && resource.BufferRevision != physical.BufferRevision)
        {
            report.ErrorResource = static_cast<uint32>(r);
            return RasterStatus::InvalidState;
        }
        tracks[r].Defined =
            resource.Kind == RasterKind::Buffer || (!resource.Transient && resource.Import.ColorDefined);
        tracks[r].DepthDefined = !resource.Transient && resource.Import.DepthDefined;
    }
    for (usize p = 0; p < graph.PassCount; ++p)
    {
        report.ErrorPass = static_cast<uint32>(p);
        const auto& pass = graph.Passes[p];
        live[p] = reference || pass.SideEffect || (!pass.IsCompute && pass.Info.Texture == RASTER_CAPACITY);
        bool attachment = !pass.IsCompute && pass.Info.Texture == RASTER_CAPACITY;
        if (attachment)
        {
            const auto& state = pass.Info.Description;
            if ((state.ColorLoad == RasterLoad::Load && !surfaceColor) ||
                (state.DepthLoad == RasterLoad::Load && !surfaceDepth))
            {
                return RasterStatus::InvalidDescription;
            }
            surfaceColor =
                state.ColorStore == RasterStore::Store &&
                (state.ColorLoad == RasterLoad::Clear || (state.ColorLoad == RasterLoad::Load && surfaceColor));
            surfaceDepth =
                state.DepthStore == RasterStore::Store &&
                (state.DepthLoad == RasterLoad::Clear || (state.DepthLoad == RasterLoad::Load && surfaceDepth));
        }
        for (usize i = 0; i < pass.UseCount; ++i)
        {
            const auto& use = pass.Uses[i];
            const auto r = GraphAccess::Resource(use.Resource);
            report.ErrorResource = static_cast<uint32>(r);
            const auto& resource = graph.Resources[r];
            auto& track = tracks[r];
            for (usize j = 0; j < i; ++j)
            {
                if (r == GraphAccess::Resource(pass.Uses[j].Resource))
                {
                    return RasterStatus::InvalidDescription;
                }
            }
            const bool write = IsWrite(use.Access);
            if (!imported[r] && resource.Kind == RasterKind::Texture &&
                resource.Import.Use != RasterTextureUse::Undefined)
            {
                const bool priorWrite = resource.Import.Use == RasterTextureUse::ColorAttachment;
                if ((priorWrite || write) &&
                    !AddDependency(report,
                                   32,
                                   p,
                                   r,
                                   priorWrite ? (write ? GraphHazard::WriteAfterWrite : GraphHazard::ReadAfterWrite)
                                              : GraphHazard::WriteAfterRead,
                                   priorWrite ? GraphAccessMode::ColorWrite : GraphAccessMode::Sampled,
                                   use.Access))
                {
                    return RasterStatus::CapacityExceeded;
                }
            }
            imported[r] = true;
            if (resource.Kind == RasterKind::Buffer)
            {
                const auto& buffer = At(resource.Kind, resource.Slot).Buffer;
                if (use.Size == 0 || !Fits(use.Offset, use.Size, buffer.Size) ||
                    (use.Access == GraphAccessMode::Vertex && buffer.Role != BufferRole::Vertex &&
                     buffer.Role != BufferRole::StorageVertex) ||
                    (use.Access == GraphAccessMode::Index && buffer.Role != BufferRole::Index16 &&
                     buffer.Role != BufferRole::Index32) ||
                    (use.Access == GraphAccessMode::Uniform && buffer.Role != BufferRole::Uniform) ||
                    (use.Access == GraphAccessMode::Indirect && buffer.Role != BufferRole::StorageIndirect) ||
                    ((use.Access == GraphAccessMode::StorageRead || use.Access == GraphAccessMode::StorageReadWrite) &&
                     (!pass.IsCompute || !StorageRole(buffer.Role) || use.Visibility != RasterVisibility::Compute)) ||
                    (pass.IsCompute && use.Access != GraphAccessMode::Uniform &&
                     use.Access != GraphAccessMode::StorageRead && use.Access != GraphAccessMode::StorageReadWrite) ||
                    (use.Access == GraphAccessMode::Sampled || use.Access == GraphAccessMode::ColorWrite ||
                     use.Access == GraphAccessMode::ColorReadWrite))
                {
                    return RasterStatus::InvalidDescription;
                }
            }
            else if (pass.IsCompute || use.Offset != 0 || use.Size != 0 ||
                     use.Visibility == RasterVisibility::Compute ||
                     (use.Access != GraphAccessMode::Sampled && use.Access != GraphAccessMode::ColorWrite &&
                      use.Access != GraphAccessMode::ColorReadWrite))
            {
                return RasterStatus::InvalidDescription;
            }
            const auto version = GraphAccess::Version(use.Resource);
            if (!write)
            {
                if (version != track.Version || !track.Defined)
                {
                    return RasterStatus::InvalidDescription;
                }
                if (track.Writer != 32 && !AddDependency(report,
                                                         track.Writer,
                                                         p,
                                                         r,
                                                         GraphHazard::ReadAfterWrite,
                                                         track.LastWrite,
                                                         use.Access))
                {
                    return RasterStatus::CapacityExceeded;
                }
                track.Readers |= uint32{1} << p;
                track.ReadAccess[p] = use.Access;
                continue;
            }
            if (version != track.Version + 1 || version == 0 ||
                (resource.Kind == RasterKind::Texture && resource.Slot != pass.Info.Texture))
            {
                return RasterStatus::InvalidDescription;
            }
            const auto& description = pass.Info.Description;
            const bool bufferWrite = resource.Kind == RasterKind::Buffer;
            const bool load = bufferWrite || description.ColorLoad == RasterLoad::Load;
            if (bufferWrite)
            {
                if (!track.Defined)
                {
                    return RasterStatus::InvalidDescription;
                }
            }
            else
            {
                attachment = true;
                if (load != (use.Access == GraphAccessMode::ColorReadWrite) || (load && !track.Defined) ||
                    (description.DepthLoad == RasterLoad::Load && !track.DepthDefined))
                {
                    return RasterStatus::InvalidDescription;
                }
            }
            if ((load || (!bufferWrite && description.DepthLoad == RasterLoad::Load)) && track.Writer != 32 &&
                !AddDependency(report, track.Writer, p, r, GraphHazard::ReadAfterWrite, track.LastWrite, use.Access))
            {
                return RasterStatus::CapacityExceeded;
            }
            if (track.Writer != 32 &&
                !AddDependency(report, track.Writer, p, r, GraphHazard::WriteAfterWrite, track.LastWrite, use.Access))
            {
                return RasterStatus::CapacityExceeded;
            }
            for (usize reader = 0; reader < p; ++reader)
            {
                if ((track.Readers & (uint32{1} << reader)) != 0 && !AddDependency(report,
                                                                                   reader,
                                                                                   p,
                                                                                   r,
                                                                                   GraphHazard::WriteAfterRead,
                                                                                   track.ReadAccess[reader],
                                                                                   use.Access))
                {
                    return RasterStatus::CapacityExceeded;
                }
            }
            track.Readers = 0;
            track.Writer = static_cast<uint32>(p);
            track.LastWrite = use.Access;
            track.Version = version;
            track.Defined = bufferWrite || (description.ColorStore == RasterStore::Store &&
                                            (description.ColorLoad == RasterLoad::Clear || (load && track.Defined)));
            track.DepthDefined = description.DepthStore == RasterStore::Store &&
                                 (description.DepthLoad == RasterLoad::Clear ||
                                  (description.DepthLoad == RasterLoad::Load && track.DepthDefined));
        }
        if (!attachment && !pass.IsCompute)
        {
            return RasterStatus::InvalidDescription;
        }
        report.ErrorResource = 16;
        if (pass.IsCompute)
        {
            const auto& set = At(RasterKind::Set, pass.Dispatch.Set).Set;
            const auto& layout = At(RasterKind::Layout, set.Layout).Layout;
            for (usize i = 0; i < layout.Count; ++i)
            {
                const auto& binding = layout.Entries[i];
                const auto access = binding.Kind == RasterBindingKind::UniformBuffer ? GraphAccessMode::Uniform
                                    : binding.Kind == RasterBindingKind::StorageRead
                                        ? GraphAccessMode::StorageRead
                                        : GraphAccessMode::StorageReadWrite;
                if (FindUse(graph, pass, RasterKind::Buffer, set.Slots[i], access, set.Offsets[i], set.Sizes[i], 4) ==
                    nullptr)
                {
                    return RasterStatus::InvalidDescription;
                }
            }
        }
        for (usize i = 0; i < pass.DrawCount; ++i)
        {
            const auto valid = CheckDeclaredPacket(graph, pass, graph.Draws[pass.FirstDraw + i]);
            if (valid != RasterStatus::Ready)
            {
                return valid;
            }
        }
    }
    report.ErrorPass = 32;
    if (!surfaceColor)
    {
        return RasterStatus::InvalidDescription;
    }
    for (usize r = 0; r < graph.ResourceCount; ++r)
    {
        report.ErrorResource = static_cast<uint32>(r);
        const auto& resource = graph.Resources[r];
        const auto& track = tracks[r];
        if (resource.Root)
        {
            if (!track.Defined || resource.RootVersion != track.Version)
            {
                return RasterStatus::InvalidDescription;
            }
            if (track.Writer != 32)
            {
                live[track.Writer] = true;
            }
        }
    }
    // Only content dependencies retain producers. Pure overwritten writes and
    // anti-dependencies cannot make unobservable work a root.
    for (usize p = graph.PassCount; p-- > 0;)
    {
        if (!live[p])
        {
            continue;
        }
        for (usize i = 0; i < report.DependencyCount; ++i)
        {
            const auto& edge = report.Dependencies[i];
            if (edge.After == p && edge.Before < p && edge.Hazard == GraphHazard::ReadAfterWrite)
            {
                live[edge.Before] = true;
            }
        }
    }
    for (usize p = 0; p < graph.PassCount; ++p)
    {
        report.Culled[p] = !live[p];
        if (!live[p])
        {
            continue;
        }
        ++report.LivePasses;
        const auto& pass = graph.Passes[p];
        for (usize i = 0; i < pass.UseCount; ++i)
        {
            const auto r = GraphAccess::Resource(pass.Uses[i].Resource);
            if (report.FirstUse[r] == 32)
            {
                report.FirstUse[r] = static_cast<uint32>(p);
            }
            report.LastUse[r] = static_cast<uint32>(p);
        }
    }
    for (usize r = 0; r < graph.ResourceCount; ++r)
    {
        const auto& resource = graph.Resources[r];
        if (resource.Kind != RasterKind::Texture || report.LastUse[r] == 32)
        {
            continue;
        }
        const auto p = report.LastUse[r];
        const auto& pass = graph.Passes[p];
        for (usize i = 0; i < pass.UseCount; ++i)
        {
            const auto& use = pass.Uses[i];
            if (GraphAccess::Resource(use.Resource) != r)
            {
                continue;
            }
            const bool write = IsWrite(use.Access), finalWrite = resource.Final == RasterTextureUse::ColorAttachment;
            if ((write || finalWrite) &&
                !AddDependency(report,
                               p,
                               32,
                               r,
                               write ? (finalWrite ? GraphHazard::WriteAfterWrite : GraphHazard::ReadAfterWrite)
                                     : GraphHazard::WriteAfterRead,
                               use.Access,
                               finalWrite ? GraphAccessMode::ColorWrite : GraphAccessMode::Sampled))
            {
                report.ErrorResource = static_cast<uint32>(r);
                return RasterStatus::CapacityExceeded;
            }
        }
    }
    report.ErrorResource = 16;
    return RasterStatus::Ready;
}
} // namespace
RasterStatus CompileOrderedGraph(DeviceHandle device, OrderedGraph handle, GraphReport& output, bool reference) noexcept
{
    GraphRecord* graph = nullptr;
    const auto admission = GraphAdmission(device, handle, graph);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    // Compile in service-owned storage rather than duplicating the report on
    // the browser stack alongside the caller's output report.
    auto& report = graph->Report;
    report.PassCount = static_cast<uint32>(graph->PassCount);
    report.LivePasses = report.DependencyCount = 0;
    report.ErrorPass = 32;
    report.ErrorResource = 16;
    for (auto& culled : report.Culled)
    {
        culled = false;
    }
    for (usize i = 0; i < GRAPH_RESOURCES; ++i)
    {
        report.FirstUse[i] = report.LastUse[i] = 32;
    }
    const auto compiled = CompileGraph(*graph, report, reference);
    output = report;
    if (compiled == RasterStatus::Ready)
    {
        graph->Compiled = true;
    }
    return compiled;
}
RasterStatus DiscardOrderedGraph(DeviceHandle device, OrderedGraph& handle) noexcept
{
    GraphRecord* graph = nullptr;
    const auto admission = GraphAdmission(device, handle, graph, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    DropGraph(*graph);
    handle = {};
    Collect();
    return RasterStatus::Ready;
}
RasterStatus ExecuteOrderedGraph(DeviceHandle device,
                                 SurfaceHandle surface,
                                 OrderedGraph& handle,
                                 SubmissionToken& completion) noexcept
{
    GraphRecord* graph = nullptr;
    const auto admission = GraphAdmission(device, handle, graph, false);
    if (admission != RasterStatus::Ready)
    {
        return admission;
    }
    if (!graph->Compiled || LifetimeAccess::Owner(completion) != 0)
    {
        return RasterStatus::InvalidState;
    }
    for (usize i = 0; i < graph->ResourceCount; ++i)
    {
        const auto& resource = graph->Resources[i];
        const auto& physical = At(resource.Kind, resource.Slot);
        if (physical.Status != RasterStatus::Ready)
        {
            return physical.Status;
        }
        if ((resource.Kind == RasterKind::Buffer &&
             (resource.BufferRevision != physical.BufferRevision || physical.BufferRevision == ~uint64{0})) ||
            (resource.Kind == RasterKind::Texture &&
             (!SameTextureState(resource.Import, TextureState(physical)) || physical.TextureRevision == ~uint64{0})))
        {
            return RasterStatus::InvalidState;
        }
    }
    for (usize p = 0; p < graph->PassCount; ++p)
    {
        if (graph->Report.Culled[p])
        {
            continue;
        }
        const auto& pass = graph->Passes[p];
        for (usize i = 0; i < pass.DrawCount; ++i)
        {
            const auto status = PacketStatus(graph->Draws[pass.FirstDraw + i]);
            if (status != RasterStatus::Ready)
            {
                return status;
            }
        }
        const auto prepared =
            pass.IsCompute ? ComputePacketStatus(pass.Dispatch) : backend::RasterPreparePass(pass.Info);
        const auto currentPrepare = Admission(device, false);
        if (currentPrepare != RasterStatus::Ready)
        {
            handle = {};
            return currentPrepare;
        }
        if (prepared != RasterStatus::Ready)
        {
            return prepared;
        }
    }
    if (gSubmissions.Next == 0)
    {
        return RasterStatus::IdentityExhausted;
    }
    const auto reserve = backend::RasterReserveSubmission();
    if (reserve != RasterStatus::Ready)
    {
        return reserve;
    }
    const auto begun = BeginFrame(device, surface);
    if (begun != DeviceStatus::Ready)
    {
        backend::RasterDiscardSubmission();
        return begun == DeviceStatus::Skipped         ? RasterStatus::NotReady
               : begun == DeviceStatus::InvalidHandle ? RasterStatus::InvalidHandle
               : begun == DeviceStatus::DeviceLost    ? RasterStatus::DeviceLost
                                                      : RasterStatus::Failed;
    }
    if (Admission(device, false) != RasterStatus::Ready)
    {
        handle = {};
        return RasterStatus::Failed;
    }
    gFrameSubmission = gSubmissions.Next;
    gGraphExecuting = true;
    if (!ClaimRasterFrame())
    {
        internal::FaultRasterSession();
        handle = {};
        return RasterStatus::Failed;
    }
    for (usize p = 0; p < graph->PassCount; ++p)
    {
        if (graph->Report.Culled[p])
        {
            continue;
        }
        const auto& pass = graph->Passes[p];
        backend::RasterEndPass();
        const auto currentEnd = Admission(device, false);
        if (currentEnd != RasterStatus::Ready)
        {
            handle = {};
            return currentEnd;
        }
        for (usize i = 0; i < pass.UseCount; ++i)
        {
            const auto& use = pass.Uses[i];
            const auto& resource = graph->Resources[GraphAccess::Resource(use.Resource)];
            if (resource.Kind == RasterKind::Buffer)
            {
                backend::ComputeBufferBarrier(resource.Slot, use.Access);
                const auto currentBuffer = Admission(device, false);
                if (currentBuffer != RasterStatus::Ready)
                {
                    handle = {};
                    return currentBuffer;
                }
                FrameRetain(RasterKind::Buffer, resource.Slot);
            }
            if (resource.Kind != RasterKind::Texture || use.Access != GraphAccessMode::Sampled)
            {
                continue;
            }
            const auto visibility = static_cast<uint8>(use.Visibility);
            const auto sampled = visibility == 1   ? RasterTextureUse::SampledVertex
                                 : visibility == 2 ? RasterTextureUse::SampledFragment
                                                   : RasterTextureUse::SampledBoth;
            backend::RasterTextureBarrier(resource.Slot, sampled);
            const auto currentBarrier = Admission(device, false);
            if (currentBarrier != RasterStatus::Ready)
            {
                handle = {};
                return currentBarrier;
            }
            FrameRetain(RasterKind::Texture, resource.Slot);
            At(RasterKind::Texture, resource.Slot).FrameTextureUse = sampled;
        }
        const auto started = pass.IsCompute ? EncodeCompute(pass.Dispatch) : backend::RasterBeginPass(pass.Info);
        if (started != RasterStatus::Ready)
        {
            internal::FaultRasterSession();
            handle = {};
            return started;
        }
        const auto current = Admission(device, false);
        if (current != RasterStatus::Ready)
        {
            handle = {};
            return current;
        }
        if (!pass.IsCompute)
        {
            ApplyPassState(pass.Info);
        }
        for (usize i = 0; i < pass.DrawCount; ++i)
        {
            const auto encoded = EncodePacket(graph->Draws[pass.FirstDraw + i]);
            const auto currentDraw = Admission(device, false);
            if (currentDraw != RasterStatus::Ready)
            {
                handle = {};
                return currentDraw;
            }
            if (encoded != RasterStatus::Ready)
            {
                internal::FaultRasterSession();
                handle = {};
                return encoded;
            }
        }
    }
    backend::RasterEndPass();
    const auto currentEnd = Admission(device, false);
    if (currentEnd != RasterStatus::Ready)
    {
        handle = {};
        return currentEnd;
    }
    for (usize r = 0; r < graph->ResourceCount; ++r)
    {
        const auto& resource = graph->Resources[r];
        if (resource.Kind == RasterKind::Buffer && graph->Report.FirstUse[r] != 32)
        {
            const auto role = At(RasterKind::Buffer, resource.Slot).Buffer.Role;
            if (StorageRole(role))
            {
                backend::ComputeBufferBarrier(resource.Slot,
                                              role == BufferRole::StorageVertex ? GraphAccessMode::Vertex
                                              : role == BufferRole::StorageIndirect
                                                  ? GraphAccessMode::Indirect
                                                  : GraphAccessMode::StorageReadWrite);
                const auto currentBuffer = Admission(device, false);
                if (currentBuffer != RasterStatus::Ready)
                {
                    handle = {};
                    return currentBuffer;
                }
            }
        }
        if (resource.Kind != RasterKind::Texture || (resource.Transient && graph->Report.FirstUse[r] == 32))
        {
            continue;
        }
        backend::RasterTextureBarrier(resource.Slot, resource.Final);
        const auto currentBarrier = Admission(device, false);
        if (currentBarrier != RasterStatus::Ready)
        {
            handle = {};
            return currentBarrier;
        }
        FrameRetain(RasterKind::Texture, resource.Slot);
        At(RasterKind::Texture, resource.Slot).FrameTextureUse = resource.Final;
    }
    const auto ended = EndFrame(device, completion);
    if (graph->Owner != 0)
    {
        DropGraph(*graph);
    }
    handle = {};
    Collect();
    if (ended != RasterStatus::Ready && GetStartup().State == StartupState::Ready)
    {
        internal::FaultRasterSession();
    }
    return ended;
}
} // namespace ludus::graphics::rhi
