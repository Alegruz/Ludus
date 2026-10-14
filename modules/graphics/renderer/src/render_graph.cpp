#include <ludus/graphics/renderer/render_graph.hpp>

#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/math/queries.hpp>
#include <ludus/foundation/math/scalar.hpp>
#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/lifetime.h>

#include <new>

// Thanks to Donald Revie, "Designing a Data-Driven Renderer", GPU Pro 3,
// V.4, sections 4.4-4.8, and Arseny Kapoulkine, "Writing an Efficient Vulkan
// Renderer", GPU Zen 2, IV.4, sections 4.4-4.6: keep scene interpretation separate
// from authored techniques and prewarm complete immutable state. This original
// adapter uses retained Ludus snapshots, not their mutable context stacks.
// Contract: docs/architecture/render-graph-authoring.md.
namespace ludus::graphics::renderer
{
using namespace foundation;
namespace format = content::render_graph_format;
namespace math = foundation::math;
using Status = rhi::RasterStatus;
namespace
{
constexpr usize ROLE_COUNT = 2;
constexpr usize MAX_COVERAGE = format::MAX_ITEMS * format::MAX_CAMERAS * ROLE_COUNT;
constexpr usize BOX_CORNER_COUNT = 8;
constexpr usize BOX_INDEX_COUNT = 36;
constexpr usize FLAT_INPUT_COUNT = 6;
constexpr usize MAX_RETAINED_SCENES = 3;
bool IsAccepted(Status status) noexcept
{
    return status == Status::Ready || status == Status::Pending;
}
Status MapDeviceStatus(rhi::DeviceStatus status) noexcept
{
    switch (status)
    {
        case rhi::DeviceStatus::Ready:
            return Status::Ready;
        case rhi::DeviceStatus::Pending:
        case rhi::DeviceStatus::Skipped:
        case rhi::DeviceStatus::NotReady:
            return Status::NotReady;
        case rhi::DeviceStatus::InvalidHandle:
            return Status::InvalidHandle;
        case rhi::DeviceStatus::InvalidState:
            return Status::InvalidState;
        case rhi::DeviceStatus::InvalidDescription:
            return Status::InvalidDescription;
        case rhi::DeviceStatus::Unsupported:
            return Status::Unsupported;
        case rhi::DeviceStatus::Failed:
            return Status::Failed;
        case rhi::DeviceStatus::DeviceLost:
            return Status::DeviceLost;
        case rhi::DeviceStatus::IdentityExhausted:
            return Status::IdentityExhausted;
    }
    return Status::InvalidDescription;
}
Status CheckDevice(rhi::DeviceHandle device) noexcept
{
    rhi::DeviceInfo information;
    const rhi::DeviceStatus status = rhi::GetDeviceInfo(device, information);
    if (status != rhi::DeviceStatus::Ready)
    {
        return MapDeviceStatus(status);
    }
    return information.FrameOpen == true ? Status::InvalidState : Status::Ready;
}
bool HasFlatInterface(const rhi::RasterShaderDescription& vertex, const rhi::RasterShaderDescription& fragment) noexcept
{
    if (vertex.Artifact.Stage != rhi::ShaderStage::Vertex || fragment.Artifact.Stage != rhi::ShaderStage::Fragment ||
        vertex.Inputs.size() != FLAT_INPUT_COUNT || vertex.Bindings.empty() == false ||
        fragment.Bindings.empty() == false)
    {
        return false;
    }
    for (usize i = 0; i < FLAT_INPUT_COUNT; ++i)
    {
        const rhi::RasterShaderInput& input = vertex.Inputs[i];
        if (input.Location != i ||
            input.Format != (i == 0 ? rhi::RasterVertexFormat::Float3 : rhi::RasterVertexFormat::Float4))
        {
            return false;
        }
    }
    return true;
}
bool HasMatchingGeometry(const RenderGraphPrograms& programs) noexcept
{
    const rhi::ShaderDescription& color = programs.ColorVertex.Artifact;
    const rhi::ShaderDescription& depth = programs.DepthVertex.Artifact;
    if (color.Wgsl != depth.Wgsl || color.Msl != depth.Msl || color.GlslEs != depth.GlslEs ||
        color.WgslEntry != depth.WgslEntry || color.MslEntry != depth.MslEntry ||
        color.GlslEsEntry != depth.GlslEsEntry || color.SpirvEntry != depth.SpirvEntry ||
        color.Spirv.size() != depth.Spirv.size())
    {
        return false;
    }
    for (usize i = 0; i < color.Spirv.size(); ++i)
    {
        if (color.Spirv[i] != depth.Spirv[i])
        {
            return false;
        }
    }
    return true;
}
Status ValidatePlan(const RenderGraphPlan& plan) noexcept
{
    if (plan.Description.Version != format::VERSION || plan.Resources.IsValid() == false ||
        plan.Passes.IsValid() == false || plan.Cameras.IsValid() == false ||
        plan.Description.ResourceCount != plan.Resources.GetSize() ||
        plan.Description.PassCount != plan.Passes.GetSize() || plan.Resources.IsEmpty() == true ||
        plan.Passes.IsEmpty() == true || plan.Resources.GetSize() > format::MAX_RESOURCES ||
        plan.Passes.GetSize() > format::MAX_PASSES || plan.Description.CameraCount != plan.Cameras.GetSize() ||
        plan.Description.CameraCount == 0 || plan.Description.CameraCount > format::MAX_CAMERAS)
    {
        return Status::InvalidDescription;
    }
    uint32 surfaceOwner = format::ABSENT;
    StaticArray<bool, format::MAX_RESOURCES> colorDefined{};
    StaticArray<bool, format::MAX_RESOURCES> depthDefined{};
    StaticArray<bool, format::MAX_CAMERAS> hasCameraRoute{};
    for (usize i = 0; i < plan.Resources.GetSize(); ++i)
    {
        const format::Resource& resource = plan.Resources[i];
        if (resource.Width == 0 || resource.Height == 0 || resource.Backing >= plan.Resources.GetSize() ||
            resource.IsSurface > 1 || resource.IsExported > 1 ||
            (resource.Kind != format::Aspect::Color && resource.Kind != format::Aspect::Depth))
        {
            return Status::InvalidDescription;
        }
        if (resource.IsSurface == 1)
        {
            if (surfaceOwner != format::ABSENT && surfaceOwner != resource.Backing)
            {
                return Status::Unsupported;
            }
            surfaceOwner = resource.Backing;
        }
        for (usize earlier = 0; earlier < i; ++earlier)
        {
            if (resource.Kind == format::Aspect::Depth && plan.Resources[earlier].Kind == format::Aspect::Depth &&
                plan.Resources[earlier].Backing == resource.Backing)
            {
                return Status::Unsupported;
            }
        }
        const format::Resource& backing = plan.Resources[resource.Backing];
        if (backing.Backing != resource.Backing || backing.Width != resource.Width ||
            backing.Height != resource.Height || backing.IsSurface != resource.IsSurface ||
            (resource.Kind == format::Aspect::Color && resource.Backing != i) ||
            (resource.IsExported == 1 && resource.IsSurface == 1))
        {
            return Status::Unsupported;
        }
    }
    for (usize camera = 0; camera < plan.Cameras.GetSize(); ++camera)
    {
        const uint32 target = plan.Cameras[camera].Target;
        if (target >= plan.Resources.GetSize() ||
            (plan.Resources[target].IsExported == 0 &&
             (plan.Resources[target].IsSurface == 0 || plan.Resources[target].Kind != format::Aspect::Color)))
        {
            return Status::InvalidDescription;
        }
    }
    for (usize passIndex = 0; passIndex < plan.Passes.GetSize(); ++passIndex)
    {
        const format::Pass& pass = plan.Passes[passIndex];
        if ((pass.Program != format::Technique::ColorAndDepth && pass.Program != format::Technique::DepthOnly) ||
            pass.Camera >= plan.Description.CameraCount || pass.Depth >= plan.Resources.GetSize() ||
            pass.AllowsRepeatedCoverage > 1 ||
            static_cast<uint32>(pass.ColorLoad) > static_cast<uint32>(format::Load::Discard) ||
            static_cast<uint32>(pass.DepthLoad) > static_cast<uint32>(format::Load::Discard) ||
            static_cast<uint32>(pass.ColorStore) > static_cast<uint32>(format::Store::Discard) ||
            static_cast<uint32>(pass.DepthStore) > static_cast<uint32>(format::Store::Discard))
        {
            return Status::InvalidDescription;
        }
        const uint32 requiredTarget = plan.Cameras[pass.Camera].Target;
        hasCameraRoute[pass.Camera] =
            hasCameraRoute[pass.Camera] == true || pass.Color == requiredTarget || pass.Depth == requiredTarget;
        const format::Resource& depth = plan.Resources[pass.Depth];
        if (depth.Kind != format::Aspect::Depth ||
            (pass.DepthLoad == format::Load::Load && depthDefined[depth.Backing] == false))
        {
            return Status::InvalidDescription;
        }
        depthDefined[depth.Backing] = pass.DepthStore == format::Store::Store &&
                                      (pass.DepthLoad == format::Load::Clear ||
                                       (pass.DepthLoad == format::Load::Load && depthDefined[depth.Backing] == true));
        if (pass.Program == format::Technique::ColorAndDepth)
        {
            if (pass.Color >= plan.Resources.GetSize())
            {
                return Status::InvalidDescription;
            }
            const format::Resource& color = plan.Resources[pass.Color];
            if (color.Kind != format::Aspect::Color || depth.Backing != pass.Color ||
                (pass.ColorLoad == format::Load::Load && colorDefined[pass.Color] == false))
            {
                return Status::Unsupported;
            }
            colorDefined[pass.Color] = pass.ColorStore == format::Store::Store &&
                                       (pass.ColorLoad == format::Load::Clear ||
                                        (pass.ColorLoad == format::Load::Load && colorDefined[pass.Color] == true));
        }
        else
        {
            if (pass.Color != format::ABSENT ||
                (pass.ColorLoad == format::Load::Load && colorDefined[depth.Backing] == false))
            {
                return Status::InvalidDescription;
            }
            colorDefined[depth.Backing] =
                pass.ColorStore == format::Store::Store &&
                (pass.ColorLoad == format::Load::Clear ||
                 (pass.ColorLoad == format::Load::Load && colorDefined[depth.Backing] == true));
        }
    }
    for (usize camera = 0; camera < plan.Description.CameraCount; ++camera)
    {
        if (hasCameraRoute[camera] == false)
        {
            return Status::InvalidDescription;
        }
    }
    for (usize i = 0; i < plan.Resources.GetSize(); ++i)
    {
        const format::Resource& resource = plan.Resources[i];
        const bool isDefined =
            resource.Kind == format::Aspect::Depth ? depthDefined[resource.Backing] : colorDefined[i];
        if ((resource.IsExported == 1 || (resource.Kind == format::Aspect::Color && resource.IsSurface == 1)) &&
            isDefined == false)
        {
            return Status::InvalidDescription;
        }
    }
    return Status::Ready;
}
} // namespace
struct RenderGraph::State final
{
    rhi::DeviceHandle Device{};
    format::Header Header{};
    StaticArray<format::Resource, format::MAX_RESOURCES> Resources{};
    StaticArray<format::Pass, format::MAX_PASSES> Passes{};
    StaticArray<rhi::TextureHandle, format::MAX_RESOURCES> Textures{};
    StaticArray<Renderer, format::TECHNIQUE_COUNT> Contexts{};
    StaticArray<bool, format::TECHNIQUE_COUNT> IsUsed{};
    StaticArray<bool, format::TECHNIQUE_COUNT> HasViews{};
    RenderGraphPrograms Programs{};
    struct Scene final
    {
        State* OwnerOrNull = nullptr;
        StaticArray<StaticArray<Mesh, format::MAX_MESHES>, format::TECHNIQUE_COUNT> Meshes{};
        StaticArray<bool, format::MAX_MESHES> HasMeshes{};
        StaticArray<Snapshot, format::MAX_PASSES> Snapshots{};
        StaticArray<PreparedView, format::MAX_PASSES> Views{};
        StaticArray<ViewDescription, format::MAX_CAMERAS> Cameras{};
        StaticArray<StaticArray<SceneItem, format::MAX_ITEMS>, format::MAX_PASSES> Items{};
        StaticArray<usize, format::MAX_PASSES> Counts{};
        StaticArray<bool, format::MAX_PASSES> HasSnapshots{};
        StaticArray<bool, format::MAX_PASSES> HasViews{};
        usize MeshCount = 0;
        uint64 Revision = 0;
        ~Scene() noexcept
        {
            if (OwnerOrNull != nullptr)
            {
                for (usize i = 0; i < OwnerOrNull->Header.PassCount; ++i)
                {
                    Renderer& context = OwnerOrNull->Contexts[static_cast<usize>(OwnerOrNull->Passes[i].Program)];
                    static_cast<void>(context.Release(Views[i]));
                    static_cast<void>(context.Release(Snapshots[i]));
                }
                for (usize i = 0; i < format::TECHNIQUE_COUNT; ++i)
                {
                    for (usize mesh = 0; mesh < MeshCount; ++mesh)
                    {
                        static_cast<void>(OwnerOrNull->Contexts[i].Release(Meshes[i][mesh]));
                    }
                }
            }
        }
    };
    core::UniquePtr<Scene> Current;
    core::UniquePtr<Scene> Pending;
    StaticArray<RenderGraphCoverageRecord, MAX_COVERAGE> Coverage{};
    usize CoverageCount = 0;
    RenderGraphReport Report{};
    bool IsReady = false;
    bool HasOpenFrame = false;
    bool HasExternalRecording = false;
    bool HasValidOutput = false;
    bool HasAttemptedDraw = false;
    uint64 RecordingRevision = 0;
    uint64 OutputRevision = 0;
    Status RecordingStatus = Status::Ready;
    rhi::SurfaceHandle RecordingSurface{};
    rhi::SubmissionToken Producer{};
    Status PollScene(Scene& inoutScene) noexcept
    {
        for (usize context = 0; context < format::TECHNIQUE_COUNT; ++context)
        {
            if (IsUsed[context] == false)
            {
                continue;
            }
            for (usize mesh = 0; mesh < inoutScene.MeshCount; ++mesh)
            {
                if (inoutScene.HasMeshes[mesh] == true)
                {
                    const Status status = Contexts[context].GetStatus(inoutScene.Meshes[context][mesh]);
                    if (status != Status::Ready)
                    {
                        return status;
                    }
                }
            }
        }
        for (usize i = 0; i < Header.PassCount; ++i)
        {
            Renderer& context = Contexts[static_cast<usize>(Passes[i].Program)];
            if (inoutScene.HasSnapshots[i] == false)
            {
                const Status created = context.CreateSnapshot(inoutScene.Items[i].GetData(),
                                                              inoutScene.Counts[i],
                                                              inoutScene.Snapshots[i]);
                if (IsAccepted(created) == false)
                {
                    return created;
                }
                inoutScene.HasSnapshots[i] = true;
            }
            if (inoutScene.HasViews[i] == false)
            {
                ViewReport report;
                const Status prepared = context.PrepareView(inoutScene.Snapshots[i],
                                                            inoutScene.Cameras[Passes[i].Camera],
                                                            inoutScene.Views[i],
                                                            report);
                if (IsAccepted(prepared) == false)
                {
                    return prepared;
                }
                inoutScene.HasViews[i] = true;
            }
            const Status ready = context.GetStatus(inoutScene.Views[i]);
            if (ready != Status::Ready)
            {
                return ready;
            }
        }
        return Status::Ready;
    }
    Status EncodePasses(rhi::SurfaceHandle surface) noexcept
    {
        RecordingSurface = surface;
        RecordingRevision = Current->Revision;
        RecordingStatus = Status::Ready;
        HasAttemptedDraw = false;
        rhi::FrameInfo information;
        const rhi::DeviceStatus acquired = rhi::GetFrameInfo(Device, surface, information);
        if (acquired != rhi::DeviceStatus::Ready)
        {
            RecordingStatus = MapDeviceStatus(acquired);
            return RecordingStatus;
        }
        for (usize resource = 0; resource < Header.ResourceCount; ++resource)
        {
            const format::Resource& description = Resources[resource];
            if (description.IsSurface == 1 &&
                (description.Width != information.Width || description.Height != information.Height))
            {
                RecordingStatus = Status::NotReady;
                return RecordingStatus;
            }
        }
        for (usize pass = 0; pass < Header.PassCount && RecordingStatus == Status::Ready; ++pass)
        {
            const format::Pass& operation = Passes[pass];
            const uint32 backing = Resources[operation.Depth].Backing;
            rhi::RasterPassDescription description;
            description.Color = Textures[backing];
            description.ColorLoad = static_cast<rhi::RasterLoad>(operation.ColorLoad);
            description.ColorStore = static_cast<rhi::RasterStore>(operation.ColorStore);
            description.DepthLoad = static_cast<rhi::RasterLoad>(operation.DepthLoad);
            description.DepthStore = static_cast<rhi::RasterStore>(operation.DepthStore);
            HasAttemptedDraw = true;
            RecordingStatus =
                Contexts[static_cast<usize>(operation.Program)].DrawView(Current->Views[pass], description);
        }
        return RecordingStatus;
    }
    Status FinishFrame(rhi::SubmissionToken& outCompletion) noexcept
    {
        const Status submitted = rhi::EndFrame(Device, outCompletion);
        if (outCompletion.IsNull() == false)
        {
            Producer = outCompletion;
        }
        rhi::FrameInfo information;
        HasOpenFrame = rhi::GetFrameInfo(Device, RecordingSurface, information) == rhi::DeviceStatus::Ready;
        if (HasOpenFrame == true)
        {
            return submitted;
        }
        if (HasAttemptedDraw == true)
        {
            HasValidOutput = submitted == Status::Ready && RecordingStatus == Status::Ready;
            OutputRevision = HasValidOutput == true ? RecordingRevision : 0;
        }
        return RecordingStatus == Status::Ready ? submitted : RecordingStatus;
    }
    ~State() noexcept
    {
        Pending.Reset();
        Current.Reset();
        for (usize i = 0; i < Header.ResourceCount; ++i)
        {
            static_cast<void>(rhi::Destroy(Device, Textures[i]));
        }
    }
};
RenderGraph::RenderGraph() noexcept = default;
RenderGraph::~RenderGraph() noexcept = default;
void RenderGraph::Reset() noexcept
{
    mState.Reset();
}
Status RenderGraph::Initialize(rhi::DeviceHandle device,
                               const RenderGraphPlan& plan,
                               const RenderGraphPrograms& programs) noexcept
{
    if (mState.Get() != nullptr)
    {
        return Status::InvalidState;
    }
    Status status = CheckDevice(device);
    if (status != Status::Ready)
    {
        return status;
    }
    status = ValidatePlan(plan);
    if (status != Status::Ready)
    {
        return status;
    }
    bool hasColor = false;
    bool hasDepth = false;
    for (usize pass = 0; pass < plan.Passes.GetSize(); ++pass)
    {
        hasColor = hasColor == true || plan.Passes[pass].Program == format::Technique::ColorAndDepth;
        hasDepth = hasDepth == true || plan.Passes[pass].Program == format::Technique::DepthOnly;
    }
    if ((hasColor == true && HasFlatInterface(programs.ColorVertex, programs.ColorFragment) == false) ||
        (hasDepth == true && HasFlatInterface(programs.DepthVertex, programs.DepthFragment) == false) ||
        (hasColor == true && hasDepth == true && HasMatchingGeometry(programs) == false))
    {
        return Status::Unsupported;
    }
    rhi::DeviceInfo information;
    if (rhi::GetDeviceInfo(device, information) != rhi::DeviceStatus::Ready)
    {
        return Status::NotReady;
    }
    for (usize i = 0; i < plan.Resources.GetSize(); ++i)
    {
        if (plan.Resources[i].IsExported == 1 && plan.Resources[i].Kind == format::Aspect::Depth &&
            information.Profile.Formats[static_cast<usize>(rhi::RasterFormat::Rgba8Unorm)].DepthSampled == false)
        {
            return Status::Unsupported;
        }
    }
    State* allocationOrNull = new (std::nothrow) State;
    core::UniquePtr<State> candidate{allocationOrNull};
    if (candidate.Get() == nullptr)
    {
        return Status::OutOfMemory;
    }
    candidate->Device = device;
    candidate->Header = plan.Description;
    candidate->Programs = programs;
    for (usize i = 0; i < plan.Resources.GetSize(); ++i)
    {
        candidate->Resources[i] = plan.Resources[i];
    }
    for (usize i = 0; i < plan.Passes.GetSize(); ++i)
    {
        candidate->Passes[i] = plan.Passes[i];
        candidate->IsUsed[static_cast<usize>(plan.Passes[i].Program)] = true;
    }
    for (usize i = 0; i < format::TECHNIQUE_COUNT; ++i)
    {
        if (candidate->IsUsed[i] == false)
        {
            continue;
        }
        const rhi::RasterShaderDescription& vertex = i == 0 ? programs.ColorVertex : programs.DepthVertex;
        const rhi::RasterShaderDescription& fragment = i == 0 ? programs.ColorFragment : programs.DepthFragment;
        Limits limits;
        limits.Objects = static_cast<uint32>(format::MAX_ITEMS);
        limits.Meshes = static_cast<uint32>(format::MAX_MESHES * MAX_RETAINED_SCENES);
        limits.Snapshots = static_cast<uint32>(format::MAX_PASSES * MAX_RETAINED_SCENES);
        limits.Views = static_cast<uint32>(format::MAX_PASSES * MAX_RETAINED_SCENES);
        status = candidate->Contexts[i].Initialize(device, vertex, fragment, limits);
        if (IsAccepted(status) == false)
        {
            return status;
        }
    }
    for (usize i = 0; i < candidate->Header.ResourceCount; ++i)
    {
        const format::Resource& resource = candidate->Resources[i];
        if (resource.Backing != i || resource.IsSurface == 1)
        {
            continue;
        }
        rhi::TextureDescription description;
        description.Width = resource.Width;
        description.Height = resource.Height;
        description.Attachment = true;
        status = rhi::CreateTexture(device, description, {}, candidate->Textures[i]);
        if (IsAccepted(status) == false)
        {
            return status;
        }
    }
    mState.Swap(candidate);
    status = Poll();
    if (IsAccepted(status) == false)
    {
        Reset();
    }
    return status;
}
Status RenderGraph::Poll() noexcept
{
    if (mState.Get() == nullptr)
    {
        return Status::InvalidState;
    }
    State& state = *mState;
    if (state.HasExternalRecording == true)
    {
        return Status::InvalidState;
    }
    const Status admitted = CheckDevice(state.Device);
    if (admitted != Status::Ready || state.HasOpenFrame == true)
    {
        return admitted == Status::Ready ? Status::InvalidState : admitted;
    }
    for (usize i = 0; i < format::TECHNIQUE_COUNT; ++i)
    {
        if (state.IsUsed[i] == false)
        {
            continue;
        }
        Status status = state.Contexts[i].Poll();
        if (status != Status::Ready)
        {
            return status;
        }
        if (state.HasViews[i] == false)
        {
            status =
                state.Contexts[i].InitializeViews(state.Programs.CompositeVertex, state.Programs.CompositeFragment);
            if (IsAccepted(status) == false)
            {
                return status;
            }
            state.HasViews[i] = true;
        }
        status = state.Contexts[i].PollViews();
        if (status != Status::Ready)
        {
            return status;
        }
    }
    for (usize i = 0; i < state.Header.ResourceCount; ++i)
    {
        if (state.Resources[i].Backing != i || state.Resources[i].IsSurface == 1)
        {
            continue;
        }
        const Status ready = rhi::GetStatus(state.Device, state.Textures[i]);
        if (ready != Status::Ready)
        {
            return ready;
        }
    }
    state.IsReady = true;
    if (state.Pending.Get() != nullptr)
    {
        const Status ready = state.PollScene(*state.Pending);
        if (ready == Status::Ready)
        {
            state.Current.Swap(state.Pending);
            state.Pending.Reset();
            if (state.Report.IsAccepted == true && state.Report.SceneRevision == state.Current->Revision)
            {
                state.Report.IsReady = true;
            }
        }
        else if (ready != Status::Pending)
        {
            if (state.Report.IsAccepted == true && state.Report.SceneRevision == state.Pending->Revision)
            {
                state.Report.IsReady = false;
            }
            state.Pending.Reset();
        }
        return ready;
    }
    return Status::Ready;
}
Status RenderGraph::Update(BorrowedView<const RenderGraphMesh> meshes,
                           BorrowedView<const RenderGraphItem> items,
                           BorrowedView<const ViewDescription> cameras,
                           uint64 sceneRevision,
                           RenderGraphCoveragePolicy policy,
                           RenderGraphReport& outReport) noexcept
{
    if (mState.Get() == nullptr)
    {
        return Status::InvalidState;
    }
    State& state = *mState;
    if (state.HasExternalRecording == true)
    {
        return Status::InvalidState;
    }
    const Status admitted = CheckDevice(state.Device);
    if (admitted != Status::Ready || state.HasOpenFrame == true || state.IsReady == false)
    {
        return admitted != Status::Ready ? admitted : Status::NotReady;
    }
    if (meshes.IsValid() == false || items.IsValid() == false || cameras.IsValid() == false ||
        meshes.GetSize() > format::MAX_MESHES || items.GetSize() > format::MAX_ITEMS ||
        cameras.GetSize() != state.Header.CameraCount || sceneRevision == 0 ||
        (policy != RenderGraphCoveragePolicy::RejectIncomplete && policy != RenderGraphCoveragePolicy::DiagnosticColor))
    {
        return Status::InvalidDescription;
    }
    for (usize cameraIndex = 0; cameraIndex < cameras.GetSize(); ++cameraIndex)
    {
        const ViewDescription& camera = cameras[cameraIndex];
        if (camera.Depth != DepthConvention::Conventional)
        {
            return Status::Unsupported;
        }
    }
    for (usize itemIndex = 0; itemIndex < items.GetSize(); ++itemIndex)
    {
        const RenderGraphItem& item = items[itemIndex];
        if (item.SourceId == 0 || item.MeshIndex >= meshes.GetSize())
        {
            return Status::InvalidDescription;
        }
    }
    state.CoverageCount = 0;
    state.Report = {};
    state.Report.SceneRevision = sceneRevision;
    for (usize camera = 0; camera < cameras.GetSize(); ++camera)
    {
        for (usize role = 0; role < ROLE_COUNT; ++role)
        {
            usize routeCount = 0;
            bool isRepeatedAllowed = true;
            for (usize pass = 0; pass < state.Header.PassCount; ++pass)
            {
                const format::Pass& operation = state.Passes[pass];
                if (operation.Camera == camera && (role == 1 || operation.Program == format::Technique::ColorAndDepth))
                {
                    ++routeCount;
                    isRepeatedAllowed = isRepeatedAllowed == true && operation.AllowsRepeatedCoverage == 1;
                }
            }
            if (routeCount == 0)
            {
                continue;
            }
            for (usize i = 0; i < items.GetSize(); ++i)
            {
                const RenderGraphItem& item = items[i];
                const RenderGraphMesh& mesh = meshes[item.MeshIndex];
                RenderGraphCoverageRecord record;
                record.SourceId = item.SourceId;
                record.Camera = static_cast<uint32>(camera);
                record.Role = role == 0 ? format::Aspect::Color : format::Aspect::Depth;
                bool hasDuplicate = false;
                for (usize other = 0; other < items.GetSize(); ++other)
                {
                    hasDuplicate = hasDuplicate == true || (other != i && items[other].SourceId == item.SourceId);
                }
                if (item.IsIntentionallyExcluded == true)
                {
                    record.Status = RenderGraphCoverage::IntentionallyExcluded;
                    ++state.Report.Excluded;
                }
                else if (hasDuplicate == true || (routeCount > 1 && isRepeatedAllowed == false))
                {
                    record.Status = RenderGraphCoverage::Conflicting;
                    record.Reason = RenderGraphReason::Duplicate;
                    ++state.Report.Conflicting;
                }
                else if (mesh.Layout != RenderGraphLayout::Static || item.Coverage != RenderGraphMaterial::Opaque)
                {
                    record.Status = RenderGraphCoverage::Unsupported;
                    record.Reason = mesh.Layout != RenderGraphLayout::Static ? RenderGraphReason::Layout
                                                                             : RenderGraphReason::Material;
                    record.HasDiagnosticRoute =
                        role == 0 && policy == RenderGraphCoveragePolicy::DiagnosticColor &&
                        (mesh.Layout == RenderGraphLayout::Static || mesh.HasDiagnosticBounds == true);
                    ++state.Report.Unsupported;
                }
                else
                {
                    record.Status = RenderGraphCoverage::Covered;
                    ++state.Report.Covered;
                }
                state.Coverage[state.CoverageCount++] = record;
            }
        }
    }
    outReport = state.Report;
    if (state.Report.Conflicting != 0 ||
        (state.Report.Unsupported != 0 && policy == RenderGraphCoveragePolicy::RejectIncomplete))
    {
        return Status::Unsupported;
    }
    State::Scene* allocationOrNull = new (std::nothrow) State::Scene;
    core::UniquePtr<State::Scene> candidate{allocationOrNull};
    if (candidate.Get() == nullptr)
    {
        return Status::OutOfMemory;
    }
    candidate->OwnerOrNull = &state;
    candidate->MeshCount = meshes.GetSize();
    candidate->Revision = sceneRevision;
    for (usize camera = 0; camera < cameras.GetSize(); ++camera)
    {
        candidate->Cameras[camera] = cameras[camera];
        candidate->Cameras[camera].OverlayToClip = cameras[camera].WorldToClip;
    }
    for (usize meshIndex = 0; meshIndex < meshes.GetSize(); ++meshIndex)
    {
        const RenderGraphMesh& mesh = meshes[meshIndex];
        bool isRequired = false;
        for (usize itemIndex = 0; itemIndex < items.GetSize(); ++itemIndex)
        {
            const RenderGraphItem& item = items[itemIndex];
            const bool isCompatible =
                mesh.Layout == RenderGraphLayout::Static && item.Coverage == RenderGraphMaterial::Opaque;
            const bool hasDiagnostic = policy == RenderGraphCoveragePolicy::DiagnosticColor &&
                                       state.IsUsed[0] == true &&
                                       (mesh.Layout == RenderGraphLayout::Static || mesh.HasDiagnosticBounds == true);
            isRequired = isRequired == true || (item.MeshIndex == meshIndex && item.IsIntentionallyExcluded == false &&
                                                (isCompatible == true || hasDiagnostic == true));
        }
        if (isRequired == false)
        {
            continue;
        }

        StaticArray<math::Vector3, BOX_CORNER_COUNT> corners{};
        const StaticArray<uint32, BOX_INDEX_COUNT> indices{{0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
                                                            2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5}};
        MeshDescription description = mesh.Geometry;
        if (mesh.Layout != RenderGraphLayout::Static)
        {
            if (mesh.HasDiagnosticBounds == false || policy != RenderGraphCoveragePolicy::DiagnosticColor)
            {
                continue;
            }
            if (math::IsFinite(mesh.BoundsMesh) == false || mesh.BoundsMesh.Min.X > mesh.BoundsMesh.Max.X ||
                mesh.BoundsMesh.Min.Y > mesh.BoundsMesh.Max.Y || mesh.BoundsMesh.Min.Z > mesh.BoundsMesh.Max.Z)
            {
                return Status::InvalidDescription;
            }
            for (usize corner = 0; corner < BOX_CORNER_COUNT; ++corner)
            {
                corners[corner] = {(corner & 1) == 0 ? mesh.BoundsMesh.Min.X : mesh.BoundsMesh.Max.X,
                                   (corner & 2) == 0 ? mesh.BoundsMesh.Min.Y : mesh.BoundsMesh.Max.Y,
                                   (corner & 4) == 0 ? mesh.BoundsMesh.Min.Z : mesh.BoundsMesh.Max.Z};
            }
            description = {};
            description.Positions = corners.GetData();
            description.VertexCount = corners.GetSize();
            description.Indices = indices.GetData();
            description.IndexCount = indices.GetSize();
        }
        candidate->HasMeshes[meshIndex] = true;
        for (usize context = 0; context < format::TECHNIQUE_COUNT; ++context)
        {
            if (state.IsUsed[context] == false)
            {
                continue;
            }
            const Status created =
                state.Contexts[context].CreateMesh(description, candidate->Meshes[context][meshIndex]);
            if (IsAccepted(created) == false)
            {
                return created;
            }
        }
    }
    for (usize pass = 0; pass < state.Header.PassCount; ++pass)
    {
        const format::Pass& operation = state.Passes[pass];
        const usize context = static_cast<usize>(operation.Program);
        for (usize itemIndex = 0; itemIndex < items.GetSize(); ++itemIndex)
        {
            const RenderGraphItem& item = items[itemIndex];
            const RenderGraphMesh& mesh = meshes[item.MeshIndex];
            if (item.IsIntentionallyExcluded == true)
            {
                continue;
            }
            const bool isCompatible =
                mesh.Layout == RenderGraphLayout::Static && item.Coverage == RenderGraphMaterial::Opaque;
            if (isCompatible == false &&
                (operation.Program == format::Technique::DepthOnly ||
                 policy != RenderGraphCoveragePolicy::DiagnosticColor || candidate->HasMeshes[item.MeshIndex] == false))
            {
                continue;
            }
            SceneItem drawing;
            drawing.SourceId = item.SourceId;
            drawing.Geometry = candidate->Meshes[context][item.MeshIndex];
            drawing.Transform = item.MeshToWorld;
            drawing.Material = item.Material;
            if (isCompatible == false)
            {
                // Existing authored flat color is runtime data, not a new shader.
                // Overlay pipeline is prewarmed and cannot establish required depth.
                drawing.Material.Color = {1, 0, 1, 1};
                drawing.Pass = Layer::Overlay;
                // OverlayToClip is the extracted camera matrix in this graph
                // profile, so world-space diagnostic geometry follows navigation.
            }
            candidate->Items[pass][candidate->Counts[pass]++] = drawing;
        }
    }
    const Status prepared = state.PollScene(*candidate);
    if (IsAccepted(prepared) == false)
    {
        return prepared;
    }
    state.Report.IsAccepted = true;
    state.Report.IsReady = prepared == Status::Ready;
    if (prepared == Status::Ready)
    {
        state.Current.Swap(candidate);
        state.Pending.Reset();
    }
    else
    {
        state.Pending.Swap(candidate);
    }
    outReport = state.Report;
    return prepared;
}
BorrowedView<const RenderGraphCoverageRecord> RenderGraph::GetCoverage() const noexcept
{
    if (mState.Get() == nullptr)
    {
        return {};
    }
    return {mState->Coverage.GetData(), mState->CoverageCount};
}
RenderGraphReport RenderGraph::GetReport() const noexcept
{
    return mState.Get() == nullptr ? RenderGraphReport{} : mState->Report;
}
uint64 RenderGraph::GetDrawableSceneRevision() const noexcept
{
    return mState.Get() == nullptr || mState->Current.Get() == nullptr ? 0 : mState->Current->Revision;
}
uint64 RenderGraph::GetOutputSceneRevision() const noexcept
{
    if (mState.Get() == nullptr || mState->HasValidOutput == false || mState->Producer.IsNull() == true ||
        rhi::GetStatus(mState->Device, mState->Producer) != Status::Ready)
    {
        return 0;
    }
    return mState->OutputRevision;
}
Status RenderGraph::Render(rhi::SurfaceHandle surface,
                           uint32 surfaceWidth,
                           uint32 surfaceHeight,
                           rhi::SubmissionToken& outCompletion) noexcept
{
    if (mState.Get() == nullptr || outCompletion.IsNull() == false)
    {
        return Status::InvalidState;
    }
    State& state = *mState;
    if (state.HasExternalRecording == true)
    {
        return Status::InvalidState;
    }
    if (state.HasOpenFrame == true)
    {
        return state.FinishFrame(outCompletion);
    }
    const Status admitted = CheckDevice(state.Device);
    if (admitted != Status::Ready)
    {
        return admitted;
    }
    if (state.IsReady == false || state.Current.Get() == nullptr)
    {
        return Status::NotReady;
    }
    if (state.Producer.IsNull() == false)
    {
        const Status completed = rhi::GetStatus(state.Device, state.Producer);
        if (completed != Status::Ready)
        {
            return completed == Status::Pending ? Status::NotReady : completed;
        }
    }
    for (usize resource = 0; resource < state.Header.ResourceCount; ++resource)
    {
        const format::Resource& description = state.Resources[resource];
        if (description.IsSurface == 1 && (description.Width != surfaceWidth || description.Height != surfaceHeight))
        {
            return Status::NotReady;
        }
    }
    const rhi::DeviceStatus started = rhi::BeginFrame(state.Device, surface);
    if (started != rhi::DeviceStatus::Ready)
    {
        return MapDeviceStatus(started);
    }
    state.HasOpenFrame = true;
    static_cast<void>(state.EncodePasses(surface));
    return state.FinishFrame(outCompletion);
}
Status RenderGraph::Encode(rhi::SurfaceHandle surface) noexcept
{
    if (mState.Get() == nullptr || mState->HasOpenFrame == true || mState->HasExternalRecording == true)
    {
        return Status::InvalidState;
    }
    State& state = *mState;
    rhi::DeviceInfo information;
    const rhi::DeviceStatus owner = rhi::GetDeviceInfo(state.Device, information);
    if (owner != rhi::DeviceStatus::Ready)
    {
        return MapDeviceStatus(owner);
    }
    if (information.FrameOpen == false)
    {
        return Status::InvalidState;
    }
    if (state.IsReady == false || state.Current.Get() == nullptr)
    {
        return Status::NotReady;
    }
    if (state.Producer.IsNull() == false)
    {
        const Status completed = rhi::GetStatus(state.Device, state.Producer);
        if (completed != Status::Ready)
        {
            return completed == Status::Pending ? Status::NotReady : completed;
        }
    }
    const Status encoded = state.EncodePasses(surface);
    state.HasExternalRecording = state.HasAttemptedDraw;
    if (state.HasExternalRecording == true)
    {
        state.HasValidOutput = false;
        state.OutputRevision = 0;
    }
    return encoded;
}
bool RenderGraph::HasUnconfirmedEncoding() const noexcept
{
    return mState.Get() != nullptr && mState->HasExternalRecording == true;
}
Status RenderGraph::ConfirmSubmission(rhi::SubmissionToken completion) noexcept
{
    if (mState.Get() == nullptr || mState->HasExternalRecording == false)
    {
        return Status::InvalidState;
    }
    State& state = *mState;
    const Status owner = CheckDevice(state.Device);
    if (owner != Status::Ready && owner != Status::DeviceLost)
    {
        return owner;
    }
    if (owner == Status::Ready && completion.IsNull() == false)
    {
        const Status submitted = rhi::GetStatus(state.Device, completion);
        if (submitted != Status::Ready && submitted != Status::Pending && submitted != Status::Failed)
        {
            return submitted;
        }
        state.Producer = completion;
        state.HasValidOutput = state.RecordingStatus == Status::Ready && submitted != Status::Failed;
        state.OutputRevision = state.HasValidOutput == true ? state.RecordingRevision : 0;
        state.HasExternalRecording = false;
        return state.RecordingStatus == Status::Ready ? submitted : state.RecordingStatus;
    }
    state.HasValidOutput = false;
    state.OutputRevision = 0;
    state.HasExternalRecording = false;
    return owner == Status::DeviceLost ? owner : Status::Failed;
}
Status RenderGraph::GetExportAllocation(uint32 resource,
                                        rhi::TextureHandle& outTexture,
                                        rhi::TextureAspect& outAspect) const noexcept
{
    if (mState.Get() == nullptr || resource >= mState->Header.ResourceCount ||
        mState->Resources[resource].IsExported == 0)
    {
        return Status::InvalidDescription;
    }
    const State& state = *mState;
    if (state.IsReady == false)
    {
        return Status::NotReady;
    }
    const rhi::TextureHandle texture = state.Textures[state.Resources[resource].Backing];
    const Status ready = rhi::GetStatus(state.Device, texture);
    if (ready != Status::Ready)
    {
        return ready == Status::Pending ? Status::NotReady : ready;
    }
    outTexture = texture;
    outAspect =
        state.Resources[resource].Kind == format::Aspect::Color ? rhi::TextureAspect::Color : rhi::TextureAspect::Depth;
    return Status::Ready;
}
Status
RenderGraph::GetExport(uint32 resource, rhi::TextureHandle& outTexture, rhi::TextureAspect& outAspect) const noexcept
{
    if (mState.Get() == nullptr || resource >= mState->Header.ResourceCount)
    {
        return Status::InvalidDescription;
    }
    const State& state = *mState;
    if (state.Resources[resource].IsExported == 0)
    {
        return Status::InvalidDescription;
    }
    if (state.HasValidOutput == false || state.Producer.IsNull() == true ||
        rhi::GetStatus(state.Device, state.Producer) != Status::Ready)
    {
        return Status::NotReady;
    }
    outTexture = state.Textures[state.Resources[resource].Backing];
    outAspect =
        state.Resources[resource].Kind == format::Aspect::Color ? rhi::TextureAspect::Color : rhi::TextureAspect::Depth;
    return Status::Ready;
}
} // namespace ludus::graphics::renderer
