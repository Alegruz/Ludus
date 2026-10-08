#include <ludus/graphics/renderer/renderer.hpp>
#include <ludus/graphics/renderer/views.hpp>

#include <ludus/foundation/containers/stable_sort.hpp>
#include <ludus/foundation/math/geometry.hpp>
#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/queries.hpp>
#include <ludus/foundation/math/scalar.hpp>
#include <ludus/foundation/math/status.hpp>
#include <ludus/foundation/math/transform.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/lifetime.h>
#include <ludus/graphics/rhi/raster.h>

#include <atomic>
#include <new>

namespace ludus::graphics::renderer
{
using namespace foundation;
namespace math = foundation::math;
using Status = rhi::RasterStatus;
namespace internal
{
struct Access final
{
    template <typename T>
    static uint64 Owner(T value) noexcept
    {
        return value.Owner;
    }
    template <typename T>
    static uint64 Generation(T value) noexcept
    {
        return value.Generation;
    }
    template <typename T>
    static usize Slot(T value) noexcept
    {
        return value.Slot;
    }
    struct Identity final
    {
        uint64 Owner, Generation;
        usize Slot;
    };
    template <typename T>
    static T Make(Identity identity) noexcept
    {
        T value;
        value.Owner = identity.Owner;
        value.Generation = identity.Generation;
        value.Slot = static_cast<uint32>(identity.Slot);
        return value;
    }
};
} // namespace internal
namespace
{
using internal::Access;
constexpr usize MESHES = 6, SNAPSHOTS = 4, VIEWS = 4, OBJECTS = 256;
constexpr uint64 LAST_ID = ~uint64{0};
// An explicit Initialize reserves identity; no resource registration at static initialization.
std::atomic<uint64> gNextOwner{1};
uint64 ReserveOwner() noexcept
{
    auto next = gNextOwner.load(std::memory_order_relaxed);
    while (next != LAST_ID)
    {
        if (gNextOwner.compare_exchange_weak(next, next + 1, std::memory_order_relaxed))
        {
            return next;
        }
    }
    return 0;
}
bool Accepted(Status status) noexcept
{
    return status == Status::Ready || status == Status::Pending;
}
bool ValidColor(math::Vector4 color) noexcept
{
    return math::IsFinite(color) && color.X >= 0 && color.X <= 1 && color.Y >= 0 && color.Y <= 1 && color.Z >= 0 &&
           color.Z <= 1 && color.W >= 0 && color.W <= 1;
}
struct SlotIdentity
{
    uint64 Generation = 1;
    bool Used = false;
    bool Owned = false;
};
template <typename T, usize N>
Status FreeSlot(const T (&slots)[N], usize& output) noexcept
{
    bool exhausted = false;
    for (usize i = 0; i < N; ++i)
    {
        if (!slots[i].Used)
        {
            if (slots[i].Generation != LAST_ID)
            {
                output = i;
                return Status::Ready;
            }
            exhausted = true;
        }
    }
    return exhausted ? Status::IdentityExhausted : Status::CapacityExceeded;
}
template <typename T, usize N, typename Handle>
T* Resolve(T (&slots)[N], uint64 owner, Handle handle) noexcept
{
    const auto index = Access::Slot(handle);
    if (Access::Owner(handle) != owner || index >= N)
    {
        return nullptr;
    }
    auto& slot = slots[index];
    return slot.Used && slot.Owned && slot.Generation == Access::Generation(handle) ? &slot : nullptr;
}
void Retire(SlotIdentity& slot) noexcept
{
    slot.Used = slot.Owned = false;
    ++slot.Generation; // LAST_ID slots are never admitted, so retirement cannot wrap.
}
math::Matrix4 Matrix(const math::Affine3& transform) noexcept
{
    return {{math::Vector4{transform.Columns[0].X, transform.Columns[0].Y, transform.Columns[0].Z, 0},
             math::Vector4{transform.Columns[1].X, transform.Columns[1].Y, transform.Columns[1].Z, 0},
             math::Vector4{transform.Columns[2].X, transform.Columns[2].Y, transform.Columns[2].Z, 0},
             math::Vector4{transform.Translation.X, transform.Translation.Y, transform.Translation.Z, 1}}};
}
struct Instance final
{
    math::Vector4 Rows[4];
    math::Vector4 Color;
};
static_assert(sizeof(math::Vector3) == 12 && sizeof(Instance) == 80);
struct Rect final
{
    float64 Left = -1, Bottom = -1, Right = 1, Top = 1;
};
Rect ProjectBounds(const math::Aabb3& bounds, const math::Matrix4& matrix, bool reverse = false) noexcept
{
    Rect result{1e300, 1e300, -1e300, -1e300};
    for (usize corner = 0; corner < 8; ++corner)
    {
        const float64 point[4]{static_cast<float64>((corner & 1) != 0 ? bounds.Max.X : bounds.Min.X),
                               static_cast<float64>((corner & 2) != 0 ? bounds.Max.Y : bounds.Min.Y),
                               static_cast<float64>((corner & 4) != 0 ? bounds.Max.Z : bounds.Min.Z),
                               1};
        float64 clip[4]{};
        for (usize row = 0; row < 4; ++row)
        {
            for (usize column = 0; column < 4; ++column)
            {
                clip[row] += static_cast<float64>(matrix.At(row, column)) * point[column];
            }
        }
        // Near-plane crossings conservatively block ordering. Projection division is setup-only.
        if (clip[3] <= 1e-6 || (reverse ? clip[3] - clip[2] <= 0 : clip[2] <= 0))
        {
            return {};
        }
        const auto x = clip[0] / clip[3], y = clip[1] / clip[3];
        if (x < result.Left)
        {
            result.Left = x;
        }
        if (x > result.Right)
        {
            result.Right = x;
        }
        if (y < result.Bottom)
        {
            result.Bottom = y;
        }
        if (y > result.Top)
        {
            result.Top = y;
        }
    }
    return result;
}
bool Overlap(const Rect& left, const Rect& right) noexcept
{
    constexpr float64 slack = 1e-5;
    return left.Right + slack >= right.Left && right.Right + slack >= left.Left && left.Top + slack >= right.Bottom &&
           right.Top + slack >= left.Bottom;
}
struct Candidate final
{
    usize Item = 0, MeshSlot = 0;
    Rect Bounds;
    Instance Data;
};
struct MeshLess final
{
    bool operator()(const Candidate& left, const Candidate& right) const noexcept
    {
        return left.MeshSlot < right.MeshSlot;
    }
};
} // namespace
struct Renderer::State final
{
    struct MeshRecord final : SlotIdentity
    {
        rhi::BufferHandle Vertices{}, Indices{};
        uint32 VertexCount = 0, IndexCount = 0, References = 0;
        math::Aabb3 Bounds;
    };
    struct Item final
    {
        SceneItem Input;
        usize MeshSlot = 0;
        math::Aabb3 Bounds;
    };
    struct SnapshotRecord final : SlotIdentity
    {
        Item Items[OBJECTS];
        usize Count = 0;
        uint32 References = 0;
    };
    struct Packet final
    {
        usize MeshSlot = 0, First = 0;
        uint32 Count = 0;
        Layer Pass = Layer::Opaque;
    };
    struct ViewRecord final : SlotIdentity
    {
        rhi::BufferHandle Instances{};
        rhi::CommandBatch Retry{};
        bool HasRetry = false;
        usize SnapshotSlot = 0, Visible = 0, DrawCount = 0;
        DepthConvention Depth = DepthConvention::Conventional;
        Packet Packets[OBJECTS];
    };
    struct PresentationRecord final : SlotIdentity
    {
        rhi::BufferHandle Vertices{};
        rhi::TextureViewHandle Texture{};
        rhi::BindingSetHandle Bindings{};
        ViewMapping Mapping;
        uint32 Phase = 0;
        Status Setup = Status::Pending;
    };
    PresentationRecord Presentations[VIEWS];
    rhi::RasterShaderHandle CompositeVertex{}, CompositeFragment{};
    rhi::BindingLayoutHandle CompositeLayout{};
    rhi::SamplerHandle CompositeSampler{};
    rhi::BufferHandle CompositeIndices{};
    rhi::RasterPipelineHandle SurfaceReverse{}, TargetOpaque{}, TargetReverse{}, TargetOverlay{}, Composite{};
    Status ViewsSetup = Status::NotReady;
    uint32 ViewsPhase = 0;
    uint64 Owner = 0;
    rhi::DeviceHandle Device;
    rhi::RasterShaderHandle Vertex{}, Fragment{};
    rhi::BindingLayoutHandle Layout{};
    rhi::BindingSetHandle Bindings{};
    rhi::RasterPipelineHandle Opaque{}, Overlay{};
    uint32 Phase = 0;
    Status Setup = Status::Pending;
    MeshRecord MeshRecords[MESHES];
    SnapshotRecord SnapshotRecords[SNAPSHOTS];
    ViewRecord ViewRecords[VIEWS];
    // Reserved once: PrepareView must fit the browser's default 64 KiB stack.
    Candidate Candidates[OBJECTS]{}, Scratch[OBJECTS]{};
    Instance Instances[OBJECTS]{};
    ~State()
    {
        for (usize i = 0; i < VIEWS; ++i)
        {
            if (ViewRecords[i].Used)
            {
                DropView(i);
            }
        }
        for (usize i = 0; i < SNAPSHOTS; ++i)
        {
            auto& scene = SnapshotRecords[i];
            if (scene.Used)
            {
                scene.Owned = false;
                DropSnapshot(i);
            }
        }
        for (auto& mesh : MeshRecords)
        {
            if (mesh.Used)
            {
                static_cast<void>(rhi::Destroy(Device, mesh.Vertices));
                static_cast<void>(rhi::Destroy(Device, mesh.Indices));
            }
        }
        for (auto& presentation : Presentations)
        {
            if (presentation.Used)
            {
                DropPresentation(presentation);
            }
        }
        const rhi::RasterPipelineHandle extra[]{SurfaceReverse, TargetOpaque, TargetReverse, TargetOverlay, Composite};
        for (auto pipeline : extra)
        {
            static_cast<void>(rhi::Destroy(Device, pipeline));
        }
        static_cast<void>(rhi::Destroy(Device, CompositeIndices));
        static_cast<void>(rhi::Destroy(Device, CompositeSampler));
        static_cast<void>(rhi::Destroy(Device, CompositeLayout));
        static_cast<void>(rhi::Destroy(Device, CompositeFragment));
        static_cast<void>(rhi::Destroy(Device, CompositeVertex));
        static_cast<void>(rhi::Destroy(Device, Overlay));
        static_cast<void>(rhi::Destroy(Device, Opaque));
        static_cast<void>(rhi::Destroy(Device, Bindings));
        static_cast<void>(rhi::Destroy(Device, Layout));
        static_cast<void>(rhi::Destroy(Device, Fragment));
        static_cast<void>(rhi::Destroy(Device, Vertex));
    }
    void DropPresentation(PresentationRecord& presentation) noexcept
    {
        static_cast<void>(rhi::Destroy(Device, presentation.Bindings));
        static_cast<void>(rhi::Destroy(Device, presentation.Texture));
        static_cast<void>(rhi::Destroy(Device, presentation.Vertices));
        Retire(presentation);
    }
    void DropMesh(usize index) noexcept
    {
        auto& mesh = MeshRecords[index];
        if (!mesh.Owned && mesh.References == 0)
        {
            static_cast<void>(rhi::Destroy(Device, mesh.Vertices));
            static_cast<void>(rhi::Destroy(Device, mesh.Indices));
            Retire(mesh);
        }
    }
    void DropSnapshot(usize index) noexcept
    {
        auto& scene = SnapshotRecords[index];
        if (!scene.Owned && scene.References == 0)
        {
            for (usize i = 0; i < scene.Count; ++i)
            {
                const auto mesh = scene.Items[i].MeshSlot;
                --MeshRecords[mesh].References;
                DropMesh(mesh);
            }
            Retire(scene);
        }
    }
    void DropView(usize index) noexcept
    {
        auto& view = ViewRecords[index];
        if (view.HasRetry)
        {
            static_cast<void>(rhi::DiscardCommands(Device, view.Retry));
        }
        if (view.Visible != 0)
        {
            static_cast<void>(rhi::Destroy(Device, view.Instances));
        }
        auto& scene = SnapshotRecords[view.SnapshotSlot];
        --scene.References;
        DropSnapshot(view.SnapshotSlot);
        view.HasRetry = false;
        Retire(view);
    }
};
Renderer::Renderer() noexcept = default;
Renderer::~Renderer() = default;
Limits Renderer::GetLimits() noexcept
{
    return {};
}
void Renderer::Reset() noexcept
{
    mState.Reset();
}
Status Renderer::Initialize(rhi::DeviceHandle device,
                            const rhi::RasterShaderDescription& vertex,
                            const rhi::RasterShaderDescription& fragment) noexcept
{
    if (mState)
    {
        return Status::InvalidState;
    }
    rhi::RasterCapabilities capabilities;
    auto status = rhi::GetRasterCapabilities(device, capabilities);
    if (status != Status::Ready)
    {
        return status;
    }
    if (capabilities.DrawsPerFrame < OBJECTS || capabilities.ResourcesPerKind < 16)
    {
        return Status::Unsupported;
    }
    if (vertex.Artifact.Stage != rhi::ShaderStage::Vertex || fragment.Artifact.Stage != rhi::ShaderStage::Fragment ||
        !vertex.Bindings.empty() || !fragment.Bindings.empty() || vertex.Inputs.size() != 6 || !fragment.Inputs.empty())
    {
        return Status::InvalidDescription;
    }
    for (usize i = 0; i < 6; ++i)
    {
        if (vertex.Inputs[i].Location != i ||
            vertex.Inputs[i].Format != (i == 0 ? rhi::RasterVertexFormat::Float3 : rhi::RasterVertexFormat::Float4))
        {
            return Status::InvalidDescription;
        }
    }
    auto* state = new (std::nothrow) State;
    if (state == nullptr)
    {
        return Status::OutOfMemory;
    }
    mState.Reset(state);
    state->Device = device;
    state->Owner = ReserveOwner();
    if (state->Owner == 0)
    {
        Reset();
        return Status::IdentityExhausted;
    }
    status = rhi::CreateRasterShader(device, vertex, state->Vertex);
    if (Accepted(status))
    {
        status = rhi::CreateRasterShader(device, fragment, state->Fragment);
    }
    if (!Accepted(status))
    {
        Reset();
        return status;
    }
    status = Poll();
    if (!Accepted(status))
    {
        Reset();
    }
    return status;
}
Status Renderer::Poll() noexcept
{
    if (!mState)
    {
        return Status::InvalidState;
    }
    auto& state = *mState;
    if (state.Setup != Status::Pending)
    {
        return state.Setup;
    }
    const rhi::RasterVertexStream streams[]{{12, false}, {80, true}};
    const rhi::RasterVertexAttribute attributes[]{{0, 0, 0, rhi::RasterVertexFormat::Float3},
                                                  {1, 1, 0, rhi::RasterVertexFormat::Float4},
                                                  {2, 1, 16, rhi::RasterVertexFormat::Float4},
                                                  {3, 1, 32, rhi::RasterVertexFormat::Float4},
                                                  {4, 1, 48, rhi::RasterVertexFormat::Float4},
                                                  {5, 1, 64, rhi::RasterVertexFormat::Float4}};
    for (;;)
    {
        Status status = Status::Ready;
        switch (state.Phase)
        {
            case 0:
                status = rhi::GetStatus(state.Device, state.Vertex);
                if (status == Status::Ready)
                {
                    status = rhi::GetStatus(state.Device, state.Fragment);
                }
                if (status == Status::Ready)
                {
                    status = rhi::CreateBindingLayout(state.Device, {}, state.Layout);
                    ++state.Phase;
                }
                break;
            case 1:
                status = rhi::GetStatus(state.Device, state.Layout);
                if (status == Status::Ready)
                {
                    status = rhi::CreateBindingSet(state.Device, state.Layout, {}, state.Bindings);
                    ++state.Phase;
                }
                break;
            case 2:
                status = rhi::GetStatus(state.Device, state.Bindings);
                if (status == Status::Ready)
                {
                    status = rhi::CreateRasterPipeline(
                        state.Device,
                        {state.Vertex, state.Fragment, state.Layout, streams, attributes, true},
                        state.Opaque);
                    ++state.Phase;
                }
                break;
            case 3:
                status = rhi::GetStatus(state.Device, state.Opaque);
                if (status == Status::Ready)
                {
                    status = rhi::CreateRasterPipeline(
                        state.Device,
                        {state.Vertex, state.Fragment, state.Layout, streams, attributes, false, true},
                        state.Overlay);
                    ++state.Phase;
                }
                break;
            default:
                status = rhi::GetStatus(state.Device, state.Overlay);
                if (status == Status::Ready)
                {
                    state.Setup = Status::Ready;
                }
                break;
        }
        if (status == Status::Pending)
        {
            return status;
        }
        if (status != Status::Ready)
        {
            state.Setup = status;
            return status;
        }
        if (state.Setup == Status::Ready)
        {
            return Status::Ready;
        }
    }
}
Status Renderer::InitializeViews(const rhi::RasterShaderDescription& vertex,
                                 const rhi::RasterShaderDescription& fragment) noexcept
{
    if (!mState || mState->Setup != Status::Ready)
    {
        return Status::NotReady;
    }
    auto& s = *mState;
    if (s.ViewsSetup != Status::NotReady)
    {
        return Status::InvalidState;
    }
    if (vertex.Artifact.Stage != rhi::ShaderStage::Vertex || fragment.Artifact.Stage != rhi::ShaderStage::Fragment ||
        vertex.Inputs.size() != 1 || vertex.Inputs[0].Location != 0 ||
        vertex.Inputs[0].Format != rhi::RasterVertexFormat::Float4 || !vertex.Bindings.empty() ||
        !fragment.Inputs.empty() || fragment.Bindings.size() != 2)
    {
        return Status::InvalidDescription;
    }
    const auto& texture = fragment.Bindings[0];
    const auto& sampler = fragment.Bindings[1];
    if (texture.Binding != 0 || texture.Kind != rhi::RasterBindingKind::Texture2D ||
        texture.Visibility != rhi::RasterVisibility::Fragment || sampler.Binding != 1 ||
        sampler.Kind != rhi::RasterBindingKind::Sampler || sampler.Visibility != rhi::RasterVisibility::Fragment ||
        texture.MinSize != 0 || sampler.MinSize != 0)
    {
        return Status::InvalidDescription;
    }
    auto status = rhi::CreateRasterShader(s.Device, vertex, s.CompositeVertex);
    if (Accepted(status))
    {
        status = rhi::CreateRasterShader(s.Device, fragment, s.CompositeFragment);
    }
    if (!Accepted(status))
    {
        static_cast<void>(rhi::Destroy(s.Device, s.CompositeVertex));
        static_cast<void>(rhi::Destroy(s.Device, s.CompositeFragment));
        s.CompositeVertex = {};
        s.CompositeFragment = {};
        return status;
    }
    s.ViewsSetup = Status::Pending;
    return PollViews();
}
Status Renderer::PollViews() noexcept
{
    if (!mState)
    {
        return Status::InvalidState;
    }
    auto& s = *mState;
    if (s.ViewsSetup != Status::Pending)
    {
        return s.ViewsSetup;
    }
    const rhi::RasterVertexStream streams[]{{12, false}, {80, true}};
    const rhi::RasterVertexAttribute attributes[]{{0, 0, 0, rhi::RasterVertexFormat::Float3},
                                                  {1, 1, 0, rhi::RasterVertexFormat::Float4},
                                                  {2, 1, 16, rhi::RasterVertexFormat::Float4},
                                                  {3, 1, 32, rhi::RasterVertexFormat::Float4},
                                                  {4, 1, 48, rhi::RasterVertexFormat::Float4},
                                                  {5, 1, 64, rhi::RasterVertexFormat::Float4}};
    const rhi::RasterVertexStream quadStreams[]{{16, false}};
    const rhi::RasterVertexAttribute quadAttributes[]{{0, 0, 0, rhi::RasterVertexFormat::Float4}};
    for (;;)
    {
        auto status = Status::Ready;
        if (s.ViewsPhase == 0)
        {
            status = rhi::GetStatus(s.Device, s.CompositeVertex);
            if (status == Status::Ready)
            {
                status = rhi::GetStatus(s.Device, s.CompositeFragment);
            }
            if (status == Status::Ready)
            {
                const rhi::RasterBinding entries[]{
                    {0, rhi::RasterBindingKind::Texture2D, rhi::RasterVisibility::Fragment, 0},
                    {1, rhi::RasterBindingKind::Sampler, rhi::RasterVisibility::Fragment, 0}};
                status = rhi::CreateBindingLayout(s.Device, entries, s.CompositeLayout);
                if (Accepted(status))
                {
                    status = rhi::CreateSampler(s.Device, {}, s.CompositeSampler);
                }
                const uint32 indices[]{0, 1, 2, 2, 3, 0};
                if (Accepted(status))
                {
                    status = rhi::CreateBuffer(s.Device,
                                               {rhi::BufferRole::Index32, sizeof(indices)},
                                               {reinterpret_cast<const uint8*>(indices), sizeof(indices)},
                                               s.CompositeIndices);
                }
                ++s.ViewsPhase;
            }
        }
        else if (s.ViewsPhase == 1)
        {
            status = rhi::GetStatus(s.Device, s.CompositeLayout);
            if (status == Status::Ready)
            {
                status = rhi::GetStatus(s.Device, s.CompositeSampler);
            }
            if (status == Status::Ready)
            {
                status = rhi::GetStatus(s.Device, s.CompositeIndices);
            }
            if (status == Status::Ready)
            {
                rhi::RasterPipelineDescription description{s.Vertex, s.Fragment, s.Layout, streams, attributes, true};
                description.DepthCompare = rhi::RasterDepthCompare::Greater;
                status = rhi::CreateRasterPipeline(s.Device, description, s.SurfaceReverse);
                ++s.ViewsPhase;
            }
        }
        else if (s.ViewsPhase >= 2 && s.ViewsPhase <= 4)
        {
            const rhi::RasterPipelineHandle previous[]{s.SurfaceReverse, s.TargetOpaque, s.TargetReverse};
            status = rhi::GetStatus(s.Device, previous[s.ViewsPhase - 2]);
            if (status == Status::Ready)
            {
                rhi::RasterPipelineDescription description{s.Vertex,
                                                           s.Fragment,
                                                           s.Layout,
                                                           streams,
                                                           attributes,
                                                           s.ViewsPhase != 4,
                                                           s.ViewsPhase == 4};
                description.Target = rhi::RasterTarget::Rgba8Unorm;
                if (s.ViewsPhase == 3)
                {
                    description.DepthCompare = rhi::RasterDepthCompare::Greater;
                }
                rhi::RasterPipelineHandle* targets[]{&s.TargetOpaque, &s.TargetReverse, &s.TargetOverlay};
                status = rhi::CreateRasterPipeline(s.Device, description, *targets[s.ViewsPhase - 2]);
                ++s.ViewsPhase;
            }
        }
        else if (s.ViewsPhase == 5)
        {
            status = rhi::GetStatus(s.Device, s.TargetOverlay);
            if (status == Status::Ready)
            {
                status = rhi::CreateRasterPipeline(
                    s.Device,
                    {s.CompositeVertex, s.CompositeFragment, s.CompositeLayout, quadStreams, quadAttributes},
                    s.Composite);
                ++s.ViewsPhase;
            }
        }
        else
        {
            status = rhi::GetStatus(s.Device, s.Composite);
            if (status == Status::Ready)
            {
                s.ViewsSetup = Status::Ready;
            }
        }
        if (status == Status::Pending)
        {
            return status;
        }
        if (status != Status::Ready)
        {
            s.ViewsSetup = status;
            return status;
        }
        if (s.ViewsSetup == Status::Ready)
        {
            return Status::Ready;
        }
    }
}
Status Renderer::DrawView(PreparedView handle, const rhi::RasterPassDescription& description) noexcept
{
    auto status = GetStatus(handle);
    if (status != Status::Ready)
    {
        return status;
    }
    if (mState->ViewsSetup != Status::Ready)
    {
        return Status::NotReady;
    }
    auto& s = *mState;
    auto& view = s.ViewRecords[Access::Slot(handle)];
    auto pass = description;
    pass.ClearDepth = view.Depth == DepthConvention::ReverseZ ? 0 : 1;
    const bool surface = rhi::IsNull(description.Color);
    if (!surface)
    {
        rhi::TextureDescription texture;
        const auto targetStatus = rhi::GetTextureDescription(s.Device, description.Color, texture);
        if (targetStatus != Status::Ready)
        {
            return targetStatus;
        }
        if (!texture.Attachment || texture.Format != rhi::RasterFormat::Rgba8Unorm)
        {
            return Status::Unsupported;
        }
    }
    status = rhi::BeginRasterPass(s.Device, pass);
    if (status != Status::Ready)
    {
        return status;
    }
    for (usize i = 0; i < view.DrawCount; ++i)
    {
        const auto& packet = view.Packets[i];
        const auto& mesh = s.MeshRecords[packet.MeshSlot];
        const rhi::RasterVertexSlice slices[]{
            {mesh.Vertices, 0, mesh.VertexCount * sizeof(math::Vector3)},
            {view.Instances, packet.First * sizeof(Instance), packet.Count * sizeof(Instance)}};
        const auto opaque = surface ? (view.Depth == DepthConvention::ReverseZ ? s.SurfaceReverse : s.Opaque)
                                    : (view.Depth == DepthConvention::ReverseZ ? s.TargetReverse : s.TargetOpaque);
        const auto overlay = surface ? s.Overlay : s.TargetOverlay;
        status = rhi::DrawIndexed(s.Device,
                                  {packet.Pass == Layer::Opaque ? opaque : overlay,
                                   s.Bindings,
                                   slices,
                                   mesh.Indices,
                                   0,
                                   mesh.IndexCount,
                                   mesh.VertexCount,
                                   packet.Count});
        if (status != Status::Ready)
        {
            return status;
        }
    }
    return Status::Ready;
}
Status
Renderer::PreparePresentation(rhi::TextureHandle source, const ViewMapping& mapping, Presentation& output) noexcept
{
    if (!mState || mState->ViewsSetup != Status::Ready)
    {
        return Status::NotReady;
    }
    if (Access::Owner(output) != 0)
    {
        return Status::InvalidState;
    }
    auto& s = *mState;
    ViewMapping resolved;
    auto status =
        ResolveViewMapping(mapping.Screen, mapping.Region, mapping.DrawableWidth, mapping.DrawableHeight, resolved);
    if (status != Status::Ready)
    {
        return status;
    }
    rhi::TextureDescription description;
    status = rhi::GetTextureDescription(s.Device, source, description);
    if (status != Status::Ready)
    {
        return status;
    }
    if (!description.Attachment || description.Format != rhi::RasterFormat::Rgba8Unorm)
    {
        return Status::Unsupported;
    }
    // Logical dimensions define the sampled image's aspect. Rendering resolution is independent.
    if (static_cast<float64>(description.Width) * static_cast<float64>(resolved.Screen.Height) !=
        static_cast<float64>(description.Height) * static_cast<float64>(resolved.Screen.Width))
    {
        return Status::InvalidDescription;
    }
    usize index = 0;
    status = FreeSlot(s.Presentations, index);
    if (status != Status::Ready)
    {
        return status;
    }
    auto& p = s.Presentations[index];
    p.Mapping = resolved;
    p.Phase = 0;
    p.Setup = Status::Pending;
    const auto sx = resolved.ClipTransform.At(0, 0), sy = resolved.ClipTransform.At(1, 1);
    const math::Vector4 vertices[]{{-sx, -sy, 0, 1}, {sx, -sy, 1, 1}, {sx, sy, 1, 0}, {-sx, sy, 0, 0}};
    status = rhi::CreateBuffer(s.Device,
                               {rhi::BufferRole::Vertex, sizeof(vertices)},
                               {reinterpret_cast<const uint8*>(vertices), sizeof(vertices)},
                               p.Vertices);
    if (Accepted(status))
    {
        status = rhi::CreateTextureView(s.Device, source, p.Texture);
    }
    if (!Accepted(status))
    {
        static_cast<void>(rhi::Destroy(s.Device, p.Texture));
        static_cast<void>(rhi::Destroy(s.Device, p.Vertices));
        p.Texture = {};
        p.Vertices = {};
        return status;
    }
    p.Used = p.Owned = true;
    output = Access::Make<Presentation>({s.Owner, p.Generation, index});
    status = GetStatus(output);
    if (!Accepted(status))
    {
        static_cast<void>(Release(output));
    }
    return status;
}
Status Renderer::GetStatus(Presentation handle) noexcept
{
    if (!mState)
    {
        return Status::InvalidHandle;
    }
    auto& s = *mState;
    auto* p = Resolve(s.Presentations, s.Owner, handle);
    if (p == nullptr)
    {
        return Status::InvalidHandle;
    }
    if (p->Setup != Status::Pending)
    {
        return p->Setup;
    }
    auto status = rhi::GetStatus(s.Device, p->Vertices);
    if (status == Status::Ready)
    {
        status = rhi::GetStatus(s.Device, p->Texture);
    }
    if (status != Status::Ready)
    {
        if (status != Status::Pending)
        {
            p->Setup = status;
        }
        return status;
    }
    if (p->Phase == 0)
    {
        const rhi::RasterBindingResource resources[]{{ .Binding = 0, .Texture = p->Texture },
                                                     { .Binding = 1, .Sampler = s.CompositeSampler }};
        status = rhi::CreateBindingSet(s.Device, s.CompositeLayout, resources, p->Bindings);
        if (!Accepted(status))
        {
            p->Setup = status;
            return status;
        }
        p->Phase = 1;
    }
    status = rhi::GetStatus(s.Device, p->Bindings);
    if (status != Status::Pending)
    {
        p->Setup = status;
    }
    return status;
}
Status Renderer::DrawPresentation(Presentation handle) noexcept
{
    if (!mState)
    {
        return Status::InvalidHandle;
    }
    auto& s = *mState;
    const auto* p = Resolve(s.Presentations, s.Owner, handle);
    if (p == nullptr)
    {
        return Status::InvalidHandle;
    }
    if (p->Setup != Status::Ready)
    {
        return p->Setup == Status::Pending ? Status::NotReady : p->Setup;
    }
    const auto extent = rhi::GetFrameInfo();
    if (extent.Width == 0 || extent.Height == 0)
    {
        return Status::InvalidState;
    }
    if (extent.Width != p->Mapping.DrawableWidth || extent.Height != p->Mapping.DrawableHeight)
    {
        return Status::NotReady;
    }
    rhi::RasterPassDescription pass;
    pass.ColorLoad = rhi::RasterLoad::Load;
    pass.Viewport = p->Mapping.Viewport;
    pass.Scissor = p->Mapping.Region;
    pass.UseViewport = pass.UseScissor = true;
    auto status = rhi::BeginRasterPass(s.Device, pass);
    if (status != Status::Ready)
    {
        return status;
    }
    const rhi::RasterVertexSlice vertices[]{{p->Vertices, 0, 4 * sizeof(math::Vector4)}};
    return rhi::DrawIndexed(s.Device, {s.Composite, p->Bindings, vertices, s.CompositeIndices, 0, 6, 4});
}
Status Renderer::Release(Presentation& handle) noexcept
{
    if (!mState)
    {
        return Status::InvalidHandle;
    }
    auto* p = Resolve(mState->Presentations, mState->Owner, handle);
    if (p == nullptr)
    {
        return Status::InvalidHandle;
    }
    mState->DropPresentation(*p);
    handle = {};
    return Status::Ready;
}

Status Renderer::CreateMesh(const MeshDescription& description, Mesh& output) noexcept
{
    if (!mState || mState->Setup != Status::Ready)
    {
        return Status::NotReady;
    }
    if (Access::Owner(output) != 0)
    {
        return Status::InvalidState;
    }
    const auto& input = description;
    rhi::RasterCapabilities caps;
    auto status = rhi::GetRasterCapabilities(mState->Device, caps);
    if (status != Status::Ready)
    {
        return status;
    }
    if (input.Positions == nullptr || input.Indices == nullptr || input.VertexCount == 0 || input.IndexCount == 0 ||
        input.IndexCount % 3 != 0 || input.VertexCount > ~uint32{0} || input.IndexCount > ~uint32{0} ||
        input.VertexCount > caps.MaxBufferSize / sizeof(math::Vector3) ||
        input.IndexCount > caps.MaxBufferSize / sizeof(uint32))
    {
        return Status::InvalidDescription;
    }
    auto bounds = math::EmptyAabb3();
    for (usize i = 0; i < input.VertexCount; ++i)
    {
        const auto position = input.Positions[i];
        if (!math::IsFinite(position))
        {
            return Status::InvalidDescription;
        }
        bounds = math::Merge(bounds, {position, position});
    }
    for (usize i = 0; i < input.IndexCount; ++i)
    {
        if (input.Indices[i] >= input.VertexCount)
        {
            return Status::InvalidDescription;
        }
    }
    usize slot = 0;
    status = FreeSlot(mState->MeshRecords, slot);
    if (status != Status::Ready)
    {
        return status;
    }
    auto& mesh = mState->MeshRecords[slot];
    const auto vertexBytes = input.VertexCount * sizeof(math::Vector3), indexBytes = input.IndexCount * sizeof(uint32);
    status = rhi::CreateBuffer(mState->Device,
                               {rhi::BufferRole::Vertex, vertexBytes},
                               {reinterpret_cast<const uint8*>(input.Positions), vertexBytes},
                               mesh.Vertices);
    if (!Accepted(status))
    {
        return status;
    }
    auto second = rhi::CreateBuffer(mState->Device,
                                    {rhi::BufferRole::Index32, indexBytes},
                                    {reinterpret_cast<const uint8*>(input.Indices), indexBytes},
                                    mesh.Indices);
    if (!Accepted(second))
    {
        static_cast<void>(rhi::Destroy(mState->Device, mesh.Vertices));
        return second;
    }
    mesh.Used = mesh.Owned = true;
    mesh.References = 0;
    mesh.VertexCount = static_cast<uint32>(input.VertexCount);
    mesh.IndexCount = static_cast<uint32>(input.IndexCount);
    mesh.Bounds = bounds;
    output = Access::Make<Mesh>({mState->Owner, mesh.Generation, slot});
    return status == Status::Pending || second == Status::Pending ? Status::Pending : Status::Ready;
}
Status Renderer::GetStatus(Mesh handle) const noexcept
{
    if (!mState)
    {
        return Status::InvalidHandle;
    }
    const auto* mesh = Resolve(mState->MeshRecords, mState->Owner, handle);
    if (mesh == nullptr)
    {
        return Status::InvalidHandle;
    }
    auto status = rhi::GetStatus(mState->Device, mesh->Vertices);
    return status == Status::Ready ? rhi::GetStatus(mState->Device, mesh->Indices) : status;
}
Status Renderer::Release(Mesh& handle) noexcept
{
    if (!mState)
    {
        return Status::InvalidHandle;
    }
    auto* mesh = Resolve(mState->MeshRecords, mState->Owner, handle);
    if (mesh == nullptr)
    {
        return Status::InvalidHandle;
    }
    mesh->Owned = false;
    mState->DropMesh(Access::Slot(handle));
    handle = {};
    return Status::Ready;
}
Status Renderer::CreateSnapshot(const SceneItem* items, usize count, Snapshot& output) noexcept
{
    if (!mState || mState->Setup != Status::Ready)
    {
        return Status::NotReady;
    }
    if (Access::Owner(output) != 0)
    {
        return Status::InvalidState;
    }
    if (count > OBJECTS)
    {
        return Status::CapacityExceeded;
    }
    if (count != 0 && items == nullptr)
    {
        return Status::InvalidDescription;
    }
    usize slot = 0;
    auto status = FreeSlot(mState->SnapshotRecords, slot);
    if (status != Status::Ready)
    {
        return status;
    }
    auto& snapshot = mState->SnapshotRecords[slot];
    for (usize i = 0; i < count; ++i)
    {
        const auto& item = items[i];
        if (item.SourceId == 0 || !math::IsFinite(item.Transform) || !ValidColor(item.Material.Color) ||
            (item.Pass != Layer::Opaque && item.Pass != Layer::Overlay) ||
            (item.Pass == Layer::Opaque && item.Material.Color.W != 1))
        {
            return Status::InvalidDescription;
        }
        for (usize j = 0; j < i; ++j)
        {
            if (items[j].SourceId == item.SourceId)
            {
                return Status::InvalidDescription;
            }
        }
        status = GetStatus(item.Geometry);
        if (status != Status::Ready)
        {
            return status == Status::Pending ? Status::NotReady : status;
        }
        auto& destination = snapshot.Items[i];
        destination.Input = item;
        destination.MeshSlot = Access::Slot(item.Geometry);
        if (math::TryTransformAabb(item.Transform,
                                   mState->MeshRecords[destination.MeshSlot].Bounds,
                                   destination.Bounds) != math::MathStatus::Success)
        {
            return Status::InvalidDescription;
        }
    }
    for (usize i = 0; i < count; ++i)
    {
        ++mState->MeshRecords[snapshot.Items[i].MeshSlot].References;
    }
    snapshot.Count = count;
    snapshot.References = 0;
    snapshot.Used = snapshot.Owned = true;
    output = Access::Make<Snapshot>({mState->Owner, snapshot.Generation, slot});
    return Status::Ready;
}
Status Renderer::Release(Snapshot& handle) noexcept
{
    if (!mState)
    {
        return Status::InvalidHandle;
    }
    auto* snapshot = Resolve(mState->SnapshotRecords, mState->Owner, handle);
    if (snapshot == nullptr)
    {
        return Status::InvalidHandle;
    }
    snapshot->Owned = false;
    mState->DropSnapshot(Access::Slot(handle));
    handle = {};
    return Status::Ready;
}
Status Renderer::PrepareView(Snapshot handle,
                             const ViewDescription& description,
                             PreparedView& output,
                             ViewReport& report) noexcept
{
    if (!mState || mState->Setup != Status::Ready)
    {
        return Status::NotReady;
    }
    if (Access::Owner(output) != 0)
    {
        return Status::InvalidState;
    }
    const auto* snapshot = Resolve(mState->SnapshotRecords, mState->Owner, handle);
    if (snapshot == nullptr)
    {
        return Status::InvalidHandle;
    }
    if (!math::IsFinite(description.WorldToClip) || !math::IsFinite(description.CullMargin) ||
        description.CullMargin < 0 || !math::IsFinite(description.OverlayToClip) ||
        static_cast<uint8>(description.Depth) > static_cast<uint8>(DepthConvention::ReverseZ) ||
        (description.InfiniteFar && description.Depth != DepthConvention::ReverseZ))
    {
        return Status::InvalidDescription;
    }
    if (description.Depth == DepthConvention::ReverseZ && mState->ViewsSetup != Status::Ready)
    {
        return Status::NotReady;
    }
    math::Frustum frustum, overlay;
    if (math::TryExtractFrustum(description.WorldToClip,
                                description.InfiniteFar ? math::FrustumMode::InfiniteReverseZPerspective
                                                        : math::FrustumMode::FinitePerspectiveOrOrthographic,
                                frustum) != math::MathStatus::Success ||
        math::TryExtractFrustum(description.OverlayToClip,
                                math::FrustumMode::FinitePerspectiveOrOrthographic,
                                overlay) != math::MathStatus::Success)
    {
        return Status::InvalidDescription;
    }
    usize slot = 0;
    auto status = FreeSlot(mState->ViewRecords, slot);
    if (status != Status::Ready)
    {
        return status;
    }
    auto& view = mState->ViewRecords[slot];
    auto& candidates = mState->Candidates;
    auto& scratch = mState->Scratch;
    usize count = 0, opaqueCount = 0;
    const Layer passes[]{Layer::Opaque, Layer::Overlay};
    for (const auto pass : passes)
    {
        for (usize i = 0; i < snapshot->Count; ++i)
        {
            const auto& item = snapshot->Items[i];
            if (item.Input.Pass != pass)
            {
                continue;
            }
            math::FrustumRelation relation;
            if (!description.Reference)
            {
                if (math::TryClassifyAabb(pass == Layer::Opaque ? frustum : overlay,
                                          item.Bounds,
                                          pass == Layer::Opaque ? description.CullMargin : 0,
                                          relation) != math::MathStatus::Success)
                {
                    return Status::InvalidDescription;
                }
                if (relation == math::FrustumRelation::Outside)
                {
                    continue;
                }
            }
            auto& candidate = candidates[count++];
            candidate.Item = i;
            candidate.MeshSlot = item.MeshSlot;
            const auto clip = (pass == Layer::Opaque ? description.WorldToClip : description.OverlayToClip) *
                              Matrix(item.Input.Transform);
            if (!math::IsFinite(clip))
            {
                return Status::InvalidDescription;
            }
            const auto rows = math::Transpose(clip);
            for (usize row = 0; row < 4; ++row)
            {
                candidate.Data.Rows[row] = rows.Columns[row];
            }
            candidate.Data.Color = item.Input.Material.Color;
            if (pass == Layer::Overlay)
            {
                candidate.Data.Color.X *= candidate.Data.Color.W;
                candidate.Data.Color.Y *= candidate.Data.Color.W;
                candidate.Data.Color.Z *= candidate.Data.Color.W;
            }
            candidate.Bounds =
                ProjectBounds(item.Bounds,
                              pass == Layer::Opaque ? description.WorldToClip : description.OverlayToClip,
                              pass == Layer::Opaque && description.Depth == DepthConvention::ReverseZ);
        }
        if (pass == Layer::Opaque)
        {
            opaqueCount = count;
        }
    }
    // Representation/context separation follows Donald Revie, "Designing a Data-Driven Renderer",
    // GPU Pro 3, V.4, sections 4.4-4.8. Thanks for the explicit pass-context distinction;
    // Ludus uses copied concrete packets rather than the chapter's general object/XML pipeline.
    // See docs/architecture/renderer-systems-gems-review.md for consulted source details.
    if (!description.Reference)
    {
        for (usize first = 0; first < opaqueCount;)
        {
            usize last = first + 1;
            for (; last < opaqueCount; ++last)
            {
                bool blocked = false;
                for (usize previous = first; previous < last; ++previous)
                {
                    if (Overlap(candidates[previous].Bounds, candidates[last].Bounds))
                    {
                        blocked = true;
                        break;
                    }
                }
                if (blocked)
                {
                    break;
                }
            }
            static_cast<void>(StableSort(candidates + first, scratch, last - first, MeshLess{}));
            first = last;
        }
    }
    ViewReport result;
    result.Submitted = static_cast<uint32>(snapshot->Count);
    result.Visible = static_cast<uint32>(count);
    result.Culled = result.Submitted - result.Visible;
    auto& instances = mState->Instances;
    view.DrawCount = 0;
    for (usize i = 0; i < count; ++i)
    {
        const auto& candidate = candidates[i];
        const auto& item = snapshot->Items[candidate.Item].Input;
        instances[i] = candidate.Data;
        result.SourceIds[i] = item.SourceId;
        if (!description.Reference && view.DrawCount != 0 &&
            view.Packets[view.DrawCount - 1].MeshSlot == candidate.MeshSlot &&
            view.Packets[view.DrawCount - 1].Pass == item.Pass)
        {
            ++view.Packets[view.DrawCount - 1].Count;
        }
        else
        {
            view.Packets[view.DrawCount++] = {candidate.MeshSlot, i, 1, item.Pass};
        }
    }
    status = Status::Ready;
    if (count != 0)
    {
        const auto bytes = count * sizeof(Instance);
        status = rhi::CreateBuffer(mState->Device,
                                   {rhi::BufferRole::Vertex, bytes},
                                   {reinterpret_cast<const uint8*>(instances), bytes},
                                   view.Instances);
        if (!Accepted(status))
        {
            return status;
        }
    }
    view.Depth = description.Depth;
    view.Visible = count;
    view.SnapshotSlot = Access::Slot(handle);
    ++mState->SnapshotRecords[view.SnapshotSlot].References;
    view.Used = view.Owned = true;
    view.HasRetry = false;
    result.Draws = static_cast<uint32>(view.DrawCount);
    report = result;
    output = Access::Make<PreparedView>({mState->Owner, view.Generation, slot});
    return status;
}
Status Renderer::GetStatus(PreparedView handle) const noexcept
{
    if (!mState)
    {
        return Status::InvalidHandle;
    }
    const auto* view = Resolve(mState->ViewRecords, mState->Owner, handle);
    if (view == nullptr)
    {
        return Status::InvalidHandle;
    }
    rhi::RasterCapabilities caps;
    const auto status = rhi::GetRasterCapabilities(mState->Device, caps);
    if (status != Status::Ready)
    {
        return status;
    }
    return view->Visible == 0 ? Status::Ready : rhi::GetStatus(mState->Device, view->Instances);
}
Status Renderer::Submit(PreparedView handle, rhi::SurfaceHandle surface, rhi::SubmissionToken& completion) noexcept
{
    auto status = GetStatus(handle);
    if (status != Status::Ready)
    {
        return status;
    }
    auto& view = mState->ViewRecords[Access::Slot(handle)];
    if (view.Depth != DepthConvention::Conventional)
    {
        return Status::InvalidDescription;
    }
    if (!view.HasRetry)
    {
        status = rhi::BeginCommands(mState->Device, view.Retry);
        if (status != Status::Ready)
        {
            return status;
        }
        view.HasRetry = true;
        for (usize i = 0; i < view.DrawCount; ++i)
        {
            const auto& packet = view.Packets[i];
            const auto& mesh = mState->MeshRecords[packet.MeshSlot];
            const rhi::RasterVertexSlice slices[]{
                {mesh.Vertices, 0, mesh.VertexCount * sizeof(math::Vector3)},
                {view.Instances, packet.First * sizeof(Instance), packet.Count * sizeof(Instance)}};
            status = rhi::RecordDraw(mState->Device,
                                     view.Retry,
                                     {packet.Pass == Layer::Opaque ? mState->Opaque : mState->Overlay,
                                      mState->Bindings,
                                      slices,
                                      mesh.Indices,
                                      0,
                                      mesh.IndexCount,
                                      mesh.VertexCount,
                                      packet.Count});
            if (status != Status::Ready)
            {
                break;
            }
        }
        if (status == Status::Ready)
        {
            status = rhi::FinishCommands(mState->Device, view.Retry);
        }
        if (status != Status::Ready)
        {
            static_cast<void>(rhi::DiscardCommands(mState->Device, view.Retry));
            view.HasRetry = false;
            return status;
        }
    }
    status = rhi::SubmitCommands(mState->Device, surface, view.Retry, completion);
    if (status == Status::Ready)
    {
        view.HasRetry = false;
    }
    else if (status != Status::NotReady)
    {
        static_cast<void>(rhi::DiscardCommands(mState->Device, view.Retry));
        view.HasRetry = false;
    }
    return status;
}
Status Renderer::Release(PreparedView& handle) noexcept
{
    if (!mState || Resolve(mState->ViewRecords, mState->Owner, handle) == nullptr)
    {
        return Status::InvalidHandle;
    }
    mState->DropView(Access::Slot(handle));
    handle = {};
    return Status::Ready;
}
} // namespace ludus::graphics::renderer
