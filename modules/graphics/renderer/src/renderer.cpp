#include <ludus/graphics/renderer/renderer.hpp>
#include <ludus/graphics/renderer/views.hpp>

#include <ludus/foundation/containers/array.hpp>
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
constexpr usize PROGRAMS = 2, MATERIALS = 8;
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
struct Vertex final
{
    math::Vector3 Position;
    math::Vector2 Uv;
};
struct Instance final
{
    math::Vector4 Rows[4];
    math::Vector4 Color;
};
static_assert(sizeof(math::Vector3) == 12 && sizeof(Vertex) == 20 && sizeof(Instance) == 80);
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
    usize Item = 0, MeshSlot = 0, MaterialSlot = MATERIALS;
    Rect Bounds;
    Instance Data;
};
struct MeshLess final
{
    bool operator()(const Candidate& left, const Candidate& right) const noexcept
    {
        return left.MaterialSlot != right.MaterialSlot ? left.MaterialSlot < right.MaterialSlot
                                                       : left.MeshSlot < right.MeshSlot;
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
        usize MeshSlot = 0, MaterialSlot = MATERIALS;
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
        usize MaterialSlot = MATERIALS;
    };
    struct ViewRecord final : SlotIdentity
    {
        rhi::BufferHandle Instances{};
        rhi::UploadTicket Upload{};
        bool UploadOwned = false;
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
    struct ProgramRecord final : SlotIdentity
    {
        rhi::RasterShaderHandle Vertex{}, Fragment{};
        rhi::BindingLayoutHandle Layout{};
        rhi::TextureHandle White{};
        rhi::PipelineRequest Requests[6]{};
        rhi::RasterPipelineHandle Pipelines[6]{};
        uint32 References = 0, Phase = 0;
        Status Setup = Status::Pending;
    };
    struct MaterialRecord final : SlotIdentity
    {
        rhi::BufferHandle Parameters{};
        rhi::SamplerHandle Sampler{};
        rhi::TextureViewHandle Texture{};
        rhi::BindingSetHandle Bindings{};
        usize ProgramSlot = 0;
        uint32 References = 0;
        MaterialAlpha Alpha = MaterialAlpha::Opaque;
        bool BindingCreated = false;
        Status Setup = Status::Pending;
    };
    ProgramRecord Programs[PROGRAMS];
    MaterialRecord Materials[MATERIALS];
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
        for (usize i = 0; i < MATERIALS; ++i)
        {
            if (Materials[i].Used)
            {
                Materials[i].Owned = false;
                DropMaterial(i);
            }
        }
        for (usize i = 0; i < PROGRAMS; ++i)
        {
            if (Programs[i].Used)
            {
                Programs[i].Owned = false;
                DropProgram(i);
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
    void DropProgram(usize index) noexcept
    {
        auto& p = Programs[index];
        if (!p.Owned && p.References == 0)
        {
            for (auto& request : p.Requests)
            {
                static_cast<void>(rhi::Release(Device, request));
            }
            static_cast<void>(rhi::Destroy(Device, p.White));
            static_cast<void>(rhi::Destroy(Device, p.Layout));
            static_cast<void>(rhi::Destroy(Device, p.Fragment));
            static_cast<void>(rhi::Destroy(Device, p.Vertex));
            const auto generation = p.Generation + 1;
            p = {};
            p.Generation = generation;
        }
    }
    void DropMaterial(usize index) noexcept
    {
        auto& m = Materials[index];
        if (!m.Owned && m.References == 0)
        {
            static_cast<void>(rhi::Destroy(Device, m.Bindings));
            static_cast<void>(rhi::Destroy(Device, m.Texture));
            static_cast<void>(rhi::Destroy(Device, m.Sampler));
            static_cast<void>(rhi::Destroy(Device, m.Parameters));
            --Programs[m.ProgramSlot].References;
            DropProgram(m.ProgramSlot);
            const auto generation = m.Generation + 1;
            m = {};
            m.Generation = generation;
        }
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
                const auto material = scene.Items[i].MaterialSlot;
                if (material != MATERIALS)
                {
                    --Materials[material].References;
                    DropMaterial(material);
                }
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
        if (view.UploadOwned)
        {
            static_cast<void>(rhi::Release(Device, view.Upload));
            view.UploadOwned = false;
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
    const rhi::RasterVertexStream streams[]{{sizeof(Vertex), false}, {80, true}};
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
    const rhi::RasterVertexStream streams[]{{sizeof(Vertex), false}, {80, true}};
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
            {mesh.Vertices, 0, mesh.VertexCount * sizeof(Vertex)},
            {view.Instances, packet.First * sizeof(Instance), packet.Count * sizeof(Instance)}};
        const auto opaque = surface ? (view.Depth == DepthConvention::ReverseZ ? s.SurfaceReverse : s.Opaque)
                                    : (view.Depth == DepthConvention::ReverseZ ? s.TargetReverse : s.TargetOpaque);
        const auto overlay = surface ? s.Overlay : s.TargetOverlay;
        auto pipeline = packet.Pass == Layer::Opaque ? opaque : overlay;
        auto bindings = s.Bindings;
        if (packet.MaterialSlot != MATERIALS)
        {
            const auto& material = s.Materials[packet.MaterialSlot];
            const auto& program = s.Programs[material.ProgramSlot];
            pipeline = program.Pipelines[(surface ? 0 : 3) + (packet.Pass == Layer::Overlay             ? 2
                                                              : view.Depth == DepthConvention::ReverseZ ? 1
                                                                                                        : 0)];
            bindings = material.Bindings;
        }
        status = rhi::DrawIndexed(
            s.Device,
            {pipeline, bindings, slices, mesh.Indices, 0, mesh.IndexCount, mesh.VertexCount, packet.Count});
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

Status Renderer::PrewarmMaterialProgram(const rhi::RasterShaderDescription& vertex,
                                        const rhi::RasterShaderDescription& fragment,
                                        MaterialProgram& output) noexcept
{
    if (!mState || mState->Setup != Status::Ready)
    {
        return Status::NotReady;
    }
    if (Access::Owner(output) != 0)
    {
        return Status::InvalidState;
    }
    if (vertex.Artifact.Stage != rhi::ShaderStage::Vertex || fragment.Artifact.Stage != rhi::ShaderStage::Fragment ||
        vertex.Inputs.size() != 7 || !vertex.Bindings.empty() || !vertex.UniformMembers.empty() ||
        !fragment.Inputs.empty() || fragment.Bindings.size() != 3 || fragment.UniformMembers.size() != 3)
    {
        return Status::InvalidDescription;
    }
    for (usize i = 0; i < 7; ++i)
    {
        const auto format = i == 0   ? rhi::RasterVertexFormat::Float3
                            : i == 6 ? rhi::RasterVertexFormat::Float2
                                     : rhi::RasterVertexFormat::Float4;
        if (vertex.Inputs[i].Location != i || vertex.Inputs[i].Format != format)
        {
            return Status::InvalidDescription;
        }
    }
    const rhi::RasterBinding expected[]{{0, rhi::RasterBindingKind::UniformBuffer, rhi::RasterVisibility::Fragment, 48},
                                        {1, rhi::RasterBindingKind::Texture2D, rhi::RasterVisibility::Fragment, 0},
                                        {2, rhi::RasterBindingKind::Sampler, rhi::RasterVisibility::Fragment, 0}};
    for (usize i = 0; i < 3; ++i)
    {
        bool found = false;
        for (const auto& binding : fragment.Bindings)
        {
            found |= binding.Binding == expected[i].Binding && binding.Kind == expected[i].Kind &&
                     binding.Visibility == expected[i].Visibility && binding.MinSize == expected[i].MinSize;
        }
        if (!found)
        {
            return Status::InvalidDescription;
        }
    }
    const char* names[]{"Tint", "UvTransform", "Options"};
    for (usize i = 0; i < 3; ++i)
    {
        bool found = false;
        for (const auto& member : fragment.UniformMembers)
        {
            found |= member.Binding == 0 && member.Name == names[i] && member.Offset == i * 16 &&
                     member.Shape == rhi::RasterUniformShape::Float4;
        }
        if (!found)
        {
            return Status::InvalidDescription;
        }
    }
    auto& s = *mState;
    usize slot = 0;
    auto status = FreeSlot(s.Programs, slot);
    if (status != Status::Ready)
    {
        return status;
    }
    auto& p = s.Programs[slot];
    p.Used = p.Owned = true;
    status = rhi::CreateRasterShader(s.Device, vertex, p.Vertex);
    if (Accepted(status))
    {
        status = rhi::CreateRasterShader(s.Device, fragment, p.Fragment);
    }
    if (Accepted(status))
    {
        status = rhi::CreateBindingLayout(s.Device, expected, p.Layout);
    }
    const uint8 white[]{255, 255, 255, 255};
    rhi::TextureDescription texture{1, 1, rhi::RasterFormat::Rgba8Unorm};
    texture.AsyncUpload = true;
    if (Accepted(status))
    {
        status = rhi::CreateTexture(s.Device, texture, {white, 4}, p.White);
    }
    if (!Accepted(status))
    {
        p.Owned = false;
        s.DropProgram(slot);
        return status;
    }
    const auto handle = Access::Make<MaterialProgram>({s.Owner, p.Generation, slot});
    status = GetStatus(handle);
    if (!Accepted(status))
    {
        p.Owned = false;
        s.DropProgram(slot);
        return status;
    }
    output = handle;
    return status;
}
Status Renderer::GetStatus(MaterialProgram handle) noexcept
{
    if (!mState)
    {
        return Status::InvalidHandle;
    }
    auto& s = *mState;
    auto* p = Resolve(s.Programs, s.Owner, handle);
    if (p == nullptr)
    {
        return Status::InvalidHandle;
    }
    rhi::RasterCapabilities caps;
    const auto device = rhi::GetRasterCapabilities(s.Device, caps);
    if (device != Status::Ready)
    {
        return device;
    }
    if (p->Setup != Status::Pending)
    {
        return p->Setup;
    }
    const Status prerequisites[]{rhi::GetStatus(s.Device, p->Vertex),
                                 rhi::GetStatus(s.Device, p->Fragment),
                                 rhi::GetStatus(s.Device, p->Layout),
                                 rhi::GetStatus(s.Device, p->White)};
    for (auto ready : prerequisites)
    {
        if (ready != Status::Ready)
        {
            if (ready != Status::Pending)
            {
                p->Setup = ready;
            }
            return ready;
        }
    }
    // Thanks to Khronos, "Pipeline Cache", Vulkan Guide (Pipeline Cache chapter),
    // https://docs.vulkan.org/guide/latest/pipeline_cache.html: make expensive
    // driver creation a prewarm obligation. We reuse R2's field-comparing cache;
    // no persisted driver blob or unbounded permutation cache is introduced.
    const rhi::RasterVertexStream streams[]{{sizeof(Vertex), false}, {sizeof(Instance), true}};
    const rhi::RasterVertexAttribute attributes[]{{0, 0, 0, rhi::RasterVertexFormat::Float3},
                                                  {1, 1, 0, rhi::RasterVertexFormat::Float4},
                                                  {2, 1, 16, rhi::RasterVertexFormat::Float4},
                                                  {3, 1, 32, rhi::RasterVertexFormat::Float4},
                                                  {4, 1, 48, rhi::RasterVertexFormat::Float4},
                                                  {5, 1, 64, rhi::RasterVertexFormat::Float4},
                                                  {6, 0, 12, rhi::RasterVertexFormat::Float2}};
    while (p->Phase <= 6)
    {
        if (p->Phase != 0)
        {
            const auto ready = rhi::GetStatus(s.Device, p->Requests[p->Phase - 1]);
            if (ready != Status::Ready)
            {
                if (ready != Status::Pending)
                {
                    p->Setup = ready;
                }
                return ready;
            }
            const auto acquired =
                rhi::GetRequestedPipeline(s.Device, p->Requests[p->Phase - 1], p->Pipelines[p->Phase - 1]);
            if (acquired != Status::Ready)
            {
                return p->Setup = acquired;
            }
        }
        if (p->Phase == 6)
        {
            return p->Setup = Status::Ready;
        }
        const auto variant = p->Phase % 3;
        rhi::RasterPipelineDescription
            description{p->Vertex, p->Fragment, p->Layout, streams, attributes, variant != 2, variant == 2};
        description.Target = p->Phase < 3 ? rhi::RasterTarget::Surface : rhi::RasterTarget::Rgba8Unorm;
        description.DepthWrite = variant != 2;
        description.DepthCompare = variant == 1 ? rhi::RasterDepthCompare::Greater : rhi::RasterDepthCompare::Less;
        const auto requested = rhi::RequestPipeline(s.Device, description, p->Requests[p->Phase]);
        if (!Accepted(requested))
        {
            return p->Setup = requested;
        }
        ++p->Phase;
        if (requested == Status::Pending)
        {
            return requested;
        }
    }
    return p->Setup;
}
Status Renderer::Release(MaterialProgram& handle) noexcept
{
    if (!mState)
    {
        return Status::InvalidHandle;
    }
    auto* p = Resolve(mState->Programs, mState->Owner, handle);
    if (p == nullptr)
    {
        return Status::InvalidHandle;
    }
    p->Owned = false;
    mState->DropProgram(Access::Slot(handle));
    handle = {};
    return Status::Ready;
}
Status Renderer::CreateMaterial(MaterialProgram program,
                                const MaterialDescription& description,
                                MaterialVersion& output) noexcept
{
    if (!mState)
    {
        return Status::NotReady;
    }
    if (Access::Owner(output) != 0)
    {
        return Status::InvalidState;
    }
    auto status = GetStatus(program);
    if (status != Status::Ready)
    {
        return status == Status::Pending ? Status::NotReady : status;
    }
    if (!ValidColor(description.Tint) || !math::IsFinite(description.UvTransform) ||
        static_cast<uint8>(description.Alpha) > 2 || !math::IsFinite(description.AlphaCutoff) ||
        description.AlphaCutoff < 0 || description.AlphaCutoff > 1 ||
        (description.Alpha == MaterialAlpha::Opaque && description.Tint.W != 1))
    {
        return Status::InvalidDescription;
    }
    auto& s = *mState;
    const auto source =
        rhi::IsNull(description.Texture) ? s.Programs[Access::Slot(program)].White : description.Texture;
    rhi::TextureDescription texture;
    status = rhi::GetTextureDescription(s.Device, source, texture);
    if (status != Status::Ready)
    {
        return status;
    }
    if (texture.Format != rhi::RasterFormat::Rgba8Unorm && texture.Format != rhi::RasterFormat::Rgba8Srgb)
    {
        return Status::InvalidDescription;
    }
    if (!math::IsFinite(description.Lod) ||
        (description.Lod != -1 &&
         (description.Lod < 0 || description.Lod > static_cast<float32>(texture.MipLevels - 1))))
    {
        return Status::InvalidDescription;
    }
    usize slot = 0;
    status = FreeSlot(s.Materials, slot);
    if (status != Status::Ready)
    {
        return status;
    }
    auto& m = s.Materials[slot];
    m.Used = m.Owned = true;
    m.ProgramSlot = Access::Slot(program);
    m.Alpha = description.Alpha;
    ++s.Programs[m.ProgramSlot].References;
    struct Parameters final
    {
        math::Vector4 Tint, UvTransform, Options;
    };
    const Parameters parameters{description.Tint,
                                description.UvTransform,
                                {description.AlphaCutoff, description.Lod, static_cast<float32>(description.Alpha), 0}};
    static_assert(sizeof(Parameters) == 48);
    status = rhi::CreateBuffer(s.Device,
                               {rhi::BufferRole::Uniform, sizeof(parameters)},
                               {reinterpret_cast<const uint8*>(&parameters), sizeof(parameters)},
                               m.Parameters);
    if (Accepted(status))
    {
        status = rhi::CreateSampler(s.Device, description.Sampler, m.Sampler);
    }
    if (Accepted(status))
    {
        status = rhi::CreateTextureView(s.Device, source, m.Texture);
    }
    if (!Accepted(status))
    {
        m.Owned = false;
        s.DropMaterial(slot);
        return status;
    }
    const auto handle = Access::Make<MaterialVersion>({s.Owner, m.Generation, slot});
    status = GetStatus(handle);
    if (!Accepted(status))
    {
        m.Owned = false;
        s.DropMaterial(slot);
        return status;
    }
    output = handle;
    return status;
}
Status Renderer::GetStatus(MaterialVersion handle) noexcept
{
    if (!mState)
    {
        return Status::InvalidHandle;
    }
    auto& s = *mState;
    auto* m = Resolve(s.Materials, s.Owner, handle);
    if (m == nullptr)
    {
        return Status::InvalidHandle;
    }
    rhi::RasterCapabilities caps;
    const auto device = rhi::GetRasterCapabilities(s.Device, caps);
    if (device != Status::Ready)
    {
        return device;
    }
    if (m->Setup != Status::Pending)
    {
        return m->Setup;
    }
    const Status prerequisites[]{rhi::GetStatus(s.Device, m->Parameters),
                                 rhi::GetStatus(s.Device, m->Sampler),
                                 rhi::GetStatus(s.Device, m->Texture)};
    for (auto ready : prerequisites)
    {
        if (ready != Status::Ready)
        {
            if (ready != Status::Pending)
            {
                m->Setup = ready;
            }
            return ready;
        }
    }
    if (!m->BindingCreated)
    {
        const rhi::RasterBindingResource values[]{{0, m->Parameters, 0, 48, {}, {}},
                                                  {1, {}, 0, 0, m->Texture, {}},
                                                  {2, {}, 0, 0, {}, m->Sampler}};
        const auto created = rhi::CreateBindingSet(s.Device, s.Programs[m->ProgramSlot].Layout, values, m->Bindings);
        if (!Accepted(created))
        {
            return m->Setup = created;
        }
        m->BindingCreated = true;
    }
    const auto ready = rhi::GetStatus(s.Device, m->Bindings);
    if (ready != Status::Pending)
    {
        m->Setup = ready;
    }
    return ready;
}
Status Renderer::PublishMaterial(MaterialVersion& candidate, MaterialVersion& current) noexcept
{
    if (!mState ||
        (Access::Owner(candidate) == Access::Owner(current) && Access::Slot(candidate) == Access::Slot(current) &&
         Access::Generation(candidate) == Access::Generation(current)))
    {
        return Status::InvalidHandle;
    }
    auto ready = GetStatus(candidate);
    if (ready != Status::Ready)
    {
        return ready == Status::Pending ? Status::NotReady : ready;
    }
    if (Access::Owner(current) != 0)
    {
        ready = Release(current);
        if (ready != Status::Ready)
        {
            return ready;
        }
    }
    current = candidate;
    candidate = {};
    return Status::Ready;
}
Status Renderer::Release(MaterialVersion& handle) noexcept
{
    if (!mState)
    {
        return Status::InvalidHandle;
    }
    auto* m = Resolve(mState->Materials, mState->Owner, handle);
    if (m == nullptr)
    {
        return Status::InvalidHandle;
    }
    m->Owned = false;
    mState->DropMaterial(Access::Slot(handle));
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
        input.VertexCount > caps.MaxBufferSize / sizeof(Vertex) ||
        input.IndexCount > caps.MaxBufferSize / sizeof(uint32))
    {
        return Status::InvalidDescription;
    }
    auto bounds = math::EmptyAabb3();
    for (usize i = 0; i < input.VertexCount; ++i)
    {
        const auto position = input.Positions[i];
        if (!math::IsFinite(position) || (input.Texcoords != nullptr && !math::IsFinite(input.Texcoords[i])))
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
    foundation::Array<Vertex> vertices;
    if (!vertices.TryResize(input.VertexCount))
    {
        return Status::OutOfMemory;
    }
    for (usize i = 0; i < input.VertexCount; ++i)
    {
        vertices[i] = {input.Positions[i], input.Texcoords == nullptr ? math::Vector2{} : input.Texcoords[i]};
    }
    const auto vertexBytes = input.VertexCount * sizeof(Vertex), indexBytes = input.IndexCount * sizeof(uint32);
    status = rhi::CreateBuffer(mState->Device,
                               {rhi::BufferRole::Vertex, vertexBytes},
                               {reinterpret_cast<const uint8*>(vertices.GetData()), vertexBytes},
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
        if (item.SourceId == 0 || !math::IsFinite(item.Transform) ||
            (item.Pass != Layer::Opaque && item.Pass != Layer::Overlay) ||
            (Access::Owner(item.Surface) == 0 &&
             (!ValidColor(item.Material.Color) || (item.Pass == Layer::Opaque && item.Material.Color.W != 1))))
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
        usize materialSlot = MATERIALS;
        if (Access::Owner(item.Surface) != 0)
        {
            status = GetStatus(item.Surface);
            if (status != Status::Ready)
            {
                return status == Status::Pending ? Status::NotReady : status;
            }
            materialSlot = Access::Slot(item.Surface);
            const auto alpha = mState->Materials[materialSlot].Alpha;
            if ((alpha == MaterialAlpha::Painter) != (item.Pass == Layer::Overlay))
            {
                return Status::InvalidDescription;
            }
        }
        auto& destination = snapshot.Items[i];
        destination.Input = item;
        destination.MeshSlot = Access::Slot(item.Geometry);
        destination.MaterialSlot = materialSlot;
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
        if (snapshot.Items[i].MaterialSlot != MATERIALS)
        {
            ++mState->Materials[snapshot.Items[i].MaterialSlot].References;
        }
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
Status Renderer::ReplaceSnapshot(const SceneItem* items, usize count, Snapshot& current) noexcept
{
    if (!mState || Resolve(mState->SnapshotRecords, mState->Owner, current) == nullptr)
    {
        return Status::InvalidHandle;
    }
    Snapshot replacement;
    const auto status = CreateSnapshot(items, count, replacement);
    if (status != Status::Ready)
    {
        return status;
    }
    const auto released = Release(current);
    if (released != Status::Ready)
    {
        static_cast<void>(Release(replacement));
        return released;
    }
    current = replacement;
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
            candidate.MaterialSlot = item.MaterialSlot;
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
            candidate.Data.Color =
                item.MaterialSlot == MATERIALS ? item.Input.Material.Color : math::Vector4{1, 1, 1, 1};
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
            view.Packets[view.DrawCount - 1].Pass == item.Pass &&
            view.Packets[view.DrawCount - 1].MaterialSlot == candidate.MaterialSlot)
        {
            ++view.Packets[view.DrawCount - 1].Count;
        }
        else
        {
            view.Packets[view.DrawCount++] = {candidate.MeshSlot, i, 1, item.Pass, candidate.MaterialSlot};
        }
    }
    status = Status::Ready;
    if (count != 0)
    {
        const auto bytes = count * sizeof(Instance);
        if (description.ScheduledUpload)
        {
            status = rhi::RequestBufferUpload(mState->Device,
                                              {rhi::BufferRole::Vertex, bytes},
                                              reinterpret_cast<const uint8*>(instances),
                                              bytes,
                                              view.Upload);
            view.UploadOwned = status == Status::Pending;
        }
        else
        {
            status = rhi::CreateBuffer(mState->Device,
                                       {rhi::BufferRole::Vertex, bytes},
                                       {reinterpret_cast<const uint8*>(instances), bytes},
                                       view.Instances);
        }
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
    auto* view = Resolve(mState->ViewRecords, mState->Owner, handle);
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
    if (view->UploadOwned)
    {
        const auto upload = rhi::GetStatus(mState->Device, view->Upload);
        if (upload != Status::Ready)
        {
            return upload;
        }
        const auto taken = rhi::TakeUploadedBuffer(mState->Device, view->Upload, view->Instances);
        if (taken != Status::Ready)
        {
            return taken;
        }
        view->UploadOwned = false;
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
                {mesh.Vertices, 0, mesh.VertexCount * sizeof(Vertex)},
                {view.Instances, packet.First * sizeof(Instance), packet.Count * sizeof(Instance)}};
            auto pipeline = packet.Pass == Layer::Opaque ? mState->Opaque : mState->Overlay;
            auto bindings = mState->Bindings;
            if (packet.MaterialSlot != MATERIALS)
            {
                const auto& material = mState->Materials[packet.MaterialSlot];
                pipeline = mState->Programs[material.ProgramSlot].Pipelines[packet.Pass == Layer::Opaque ? 0 : 2];
                bindings = material.Bindings;
            }
            status = rhi::RecordDraw(
                mState->Device,
                view.Retry,
                {pipeline, bindings, slices, mesh.Indices, 0, mesh.IndexCount, mesh.VertexCount, packet.Count});
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
