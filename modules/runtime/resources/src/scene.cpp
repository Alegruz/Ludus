#include <ludus/runtime/resources/scene.h>

#include <ludus/content/scene_format/luscn.h>
#include <ludus/foundation/base/byte_order.hpp>
#include <ludus/foundation/math/projection.hpp>
#include <ludus/foundation/math/scalar.hpp>

#include <atomic>
#include <cstring>
#include <new>

namespace ludus::resources
{
// Thanks to James Boer, "Resource and Memory Management", Game Programming Gems 1,
// section 1.7, pp. 80-87, for retaining metadata independently of payload residency
// and explicit lifetime holds. See docs/architecture/resource-management-reference-review.md.
// This original aggregate CPU adapter owns no GPU objects. Project-level exact
// artifact/dependency contracts: docs/architecture/resource-management.md.
namespace
{
namespace math = foundation::math;
namespace format = content::scene_format;
// The decoded in-memory types must hold exactly the LUSCN wire components.
static_assert(sizeof(format::VertexRecord) == 2 * sizeof(math::Vector3) + sizeof(math::Vector2));
static_assert(sizeof(format::ObjectRecord::Linear) == sizeof(math::Affine3::Columns) &&
              sizeof(format::ObjectRecord::Translation) == sizeof(math::Affine3::Translation));
static_assert(sizeof(format::Header::Eye) == sizeof(math::Vector3));
class Reader final
{
public:
    Reader(const uint8* bytes, usize count) noexcept : mBytes(bytes), mRemaining(count) {}
    const uint8* Take(usize count) noexcept
    {
        if (count > mRemaining)
        {
            return nullptr;
        }
        const auto* result = mBytes;
        mBytes += count;
        mRemaining -= count;
        return result;
    }
    bool Read(uint32& value) noexcept
    {
        const auto* bytes = Take(4);
        return bytes != nullptr && foundation::core::TryReadLittleEndian<uint32>({bytes, 4}, value) == true;
    }
    bool Read(float32& value) noexcept
    {
        uint32 bits = 0;
        if (Read(bits) == false)
        {
            return false;
        }
        std::memcpy(&value, &bits, sizeof(value));
        return math::IsFinite(value);
    }
    bool Read(math::Vector3& value) noexcept
    {
        return Read(value.X) == true && Read(value.Y) == true && Read(value.Z) == true;
    }
    [[nodiscard]] usize Remaining() const noexcept
    {
        return mRemaining;
    }

private:
    const uint8* mBytes;
    usize mRemaining;
};
std::atomic<uint64> NextEpoch{1};
uint64 ReserveEpoch() noexcept
{
    auto next = NextEpoch.load(std::memory_order_relaxed);
    while (next != 0)
    {
        const uint64 following = next == ~uint64{0} ? 0 : next + 1;
        if (NextEpoch.compare_exchange_weak(next, following, std::memory_order_relaxed) == true)
        {
            return next;
        }
    }
    return 0;
}
} // namespace
Status SceneDefinition::Decode(const uint8* bytes, usize count, SceneDefinition& output) noexcept
{
    if (bytes == nullptr || count < sizeof(format::Header) || count > format::MAX_BUNDLE_BYTES)
    {
        return Status::Failed;
    }
    Reader reader(bytes, count);
    const auto* magic = reader.Take(format::MAGIC.size());
    uint32 version = 0, meshes = 0, materials = 0, textures = 0, objects = 0;
    SceneDefinition scene;
    if (std::memcmp(magic, format::MAGIC.data(), format::MAGIC.size()) != 0 || reader.Read(version) == false ||
        (version != format::VERSION_1 && version != format::VERSION_2) || reader.Read(meshes) == false || meshes == 0 ||
        meshes > format::MAX_MESHES || reader.Read(materials) == false || materials == 0 ||
        materials > format::MAX_MATERIALS || reader.Read(textures) == false || textures > format::MAX_TEXTURES ||
        reader.Read(objects) == false || objects == 0 || objects > format::MAX_OBJECTS ||
        reader.Read(scene.Eye) == false || reader.Read(scene.Target) == false || reader.Read(scene.Up) == false ||
        reader.Read(scene.VerticalFov) == false)
    {
        return Status::Failed;
    }
    math::Matrix4 view, projection;
    if (math::TryLookAt(scene.Eye, scene.Target, scene.Up, view) != math::MathStatus::Success ||
        math::TryPerspectiveReverseZ(scene.VerticalFov, 1, .05F, 100, projection) != math::MathStatus::Success)
    {
        return Status::Failed;
    }
    if (scene.Textures.TryResize(textures) == false || scene.Materials.TryResize(materials) == false ||
        scene.Meshes.TryResize(meshes) == false || scene.Objects.TryResize(objects) == false)
    {
        return Status::OutOfMemory;
    }
    usize textureBytes = 0;
    for (auto& texture : scene.Textures)
    {
        uint32 size = 0;
        if (reader.Read(texture.Width) == false || reader.Read(texture.Height) == false || reader.Read(size) == false ||
            texture.Width == 0 || texture.Height == 0 || texture.Width > format::MAX_TEXTURE_DIMENSION ||
            texture.Height > format::MAX_TEXTURE_DIMENSION ||
            size != usize(texture.Width) * texture.Height * format::TEXEL_BYTES || size > format::MAX_TEXTURE_BYTES ||
            textureBytes + size > format::MAX_TOTAL_TEXTURE_BYTES)
        {
            return Status::Failed;
        }
        const auto* pixels = reader.Take(size);
        if (pixels == nullptr)
        {
            return Status::Failed;
        }
        if (texture.Pixels.TryResize(size) == false)
        {
            return Status::OutOfMemory;
        }
        std::memcpy(texture.Pixels.GetData(), pixels, size);
        textureBytes += size;
    }
    for (auto& material : scene.Materials)
    {
        uint32 address = 0;
        if (reader.Read(material.Texture) == false ||
            (version == format::VERSION_2 && (reader.Read(address) == false || address > format::ADDRESS_REPEAT)))
        {
            return Status::Failed;
        }
        material.Repeat = version == format::VERSION_2 ? address == format::ADDRESS_REPEAT
                                                       : material.Texture > 0 && material.Texture < textures;
        if (reader.Read(material.Metallic) == false || reader.Read(material.Roughness) == false ||
            reader.Read(material.Emission) == false ||
            (material.Texture != format::NO_TEXTURE && material.Texture >= textures) || material.Metallic < 0 ||
            material.Metallic > 1 || material.Roughness < format::MIN_ROUGHNESS || material.Roughness > 1 ||
            material.Emission.X < 0 || material.Emission.Y < 0 || material.Emission.Z < 0 ||
            material.Emission.X > format::MAX_EMISSION || material.Emission.Y > format::MAX_EMISSION ||
            material.Emission.Z > format::MAX_EMISSION)
        {
            return Status::Failed;
        }
    }
    usize vertexTotal = 0;
    for (auto& mesh : scene.Meshes)
    {
        uint32 vertices = 0, indices = 0;
        if (reader.Read(vertices) == false || reader.Read(indices) == false || vertices == 0 ||
            vertices > format::MAX_VERTICES || indices == 0 || indices > format::MAX_INDICES || indices % 3 != 0 ||
            vertexTotal + vertices > format::MAX_VERTICES ||
            usize(vertices) * sizeof(format::VertexRecord) + usize(indices) * sizeof(uint32) > reader.Remaining())
        {
            return Status::Failed;
        }
        if (mesh.Positions.TryResize(vertices) == false || mesh.Normals.TryResize(vertices) == false ||
            mesh.Texcoords.TryResize(vertices) == false || mesh.Indices.TryResize(indices) == false)
        {
            return Status::OutOfMemory;
        }
        for (usize i = 0; i < vertices; ++i)
        {
            if (reader.Read(mesh.Positions[i]) == false || reader.Read(mesh.Normals[i]) == false ||
                reader.Read(mesh.Texcoords[i].X) == false || reader.Read(mesh.Texcoords[i].Y) == false ||
                (mesh.Normals[i].X == 0 && mesh.Normals[i].Y == 0 && mesh.Normals[i].Z == 0))
            {
                return Status::Failed;
            }
        }
        for (auto& index : mesh.Indices)
        {
            if (reader.Read(index) == false || index >= vertices)
            {
                return Status::Failed;
            }
        }
        vertexTotal += vertices;
    }
    for (auto& object : scene.Objects)
    {
        if (reader.Read(object.Mesh) == false || object.Mesh >= meshes || reader.Read(object.Material) == false ||
            object.Material >= materials)
        {
            return Status::Failed;
        }
        for (auto& column : object.Transform.Columns)
        {
            if (reader.Read(column) == false)
            {
                return Status::Failed;
            }
        }
        math::Affine3 inverse;
        if (reader.Read(object.Transform.Translation) == false ||
            math::TryInverse(object.Transform, inverse) != math::MathStatus::Success)
        {
            return Status::Failed;
        }
    }
    if (reader.Remaining() != 0)
    {
        return Status::Failed;
    }
    output = foundation::Move(scene);
    return Status::Ok;
}
uint64 SceneDefinition::Bytes() const noexcept
{
    uint64 bytes = Textures.GetSize() * sizeof(SceneTexture) + Materials.GetSize() * sizeof(SceneMaterial) +
                   Meshes.GetSize() * sizeof(SceneMesh) + Objects.GetSize() * sizeof(SceneObject);
    for (const auto& texture : Textures)
    {
        bytes += texture.Pixels.GetSize();
    }
    for (const auto& mesh : Meshes)
    {
        bytes += mesh.Positions.GetSize() * 32 + mesh.Indices.GetSize() * 4;
    }
    return bytes;
}
struct SceneDefinitionAdapter::State final
{
    struct Job final
    {
        bool Used = false;
        bool Cancelled = false;
    };
    content::Resource Resource;
    const SceneDefinition* Scene = nullptr;
    uint64 Epoch = 0;
    Job Jobs[16];
};
SceneDefinitionAdapter::~SceneDefinitionAdapter() noexcept
{
    if (mState != nullptr)
    {
        for (const auto& job : mState->Jobs)
        {
            LUDUS_REQUIRE(!job.Used);
        }
    }
    delete mState;
}
Status SceneDefinitionAdapter::Initialize(const content::Resource& resource, const SceneDefinition& scene) noexcept
{
    if (mState != nullptr || resource.Type != content::Kind::CookedScene ||
        content::ValidId(resource.Id.View()) == false || content::ValidPath(resource.Path.View()) == false ||
        scene.Meshes.IsEmpty() == true || scene.Objects.IsEmpty() == true || scene.Bytes() > uint64{64} * 1024 * 1024)
    {
        return Status::InvalidState;
    }
    const auto epoch = ReserveEpoch();
    if (epoch == 0)
    {
        return Status::LimitExceeded;
    }
    auto* state = new (std::nothrow) State;
    if (state == nullptr)
    {
        return Status::OutOfMemory;
    }
    state->Resource = resource;
    state->Scene = &scene;
    state->Epoch = epoch;
    mState = state;
    return Status::Ok;
}
Adapter SceneDefinitionAdapter::Descriptor() noexcept
{
    Adapter adapter;
    adapter.Type = content::Kind::CookedScene;
    adapter.Context = this;
    adapter.Epoch = Epoch;
    adapter.Begin = Begin;
    adapter.Poll = Poll;
    adapter.Cancel = Cancel;
    adapter.Retire = Retire;
    adapter.Validate = Validate;
    return adapter;
}
uint64 SceneDefinitionAdapter::Epoch(void* context) noexcept
{
    const auto* state = static_cast<SceneDefinitionAdapter*>(context)->mState;
    return state == nullptr ? 0 : state->Epoch;
}
Status SceneDefinitionAdapter::Begin(void* context, const content::Resource& resource, void*& candidate) noexcept
{
    auto* state = static_cast<SceneDefinitionAdapter*>(context)->mState;
    if (state == nullptr || resource.Id.View() != state->Resource.Id.View() ||
        resource.Path.View() != state->Resource.Path.View() || resource.Type != content::Kind::CookedScene)
    {
        return Status::NotFound;
    }
    for (auto& job : state->Jobs)
    {
        if (job.Used == false)
        {
            job = { .Used = true, .Cancelled = false };
            candidate = &job;
            return Status::Ok;
        }
    }
    return Status::QueueFull;
}
Status SceneDefinitionAdapter::Poll(void* context, void* candidate, const void*& payload) noexcept
{
    if (context == candidate)
    {
        return Status::InvalidHandle;
    }
    auto* state = static_cast<SceneDefinitionAdapter*>(context)->mState;
    const auto& job = *static_cast<State::Job*>(candidate);
    if (job.Used == false || job.Cancelled == true)
    {
        return Status::Cancelled;
    }
    payload = state->Scene;
    return Status::Ok;
}
void SceneDefinitionAdapter::Cancel(void*, void* candidate) noexcept
{
    static_cast<State::Job*>(candidate)->Cancelled = true;
}
Status SceneDefinitionAdapter::Retire(void*, void* candidate) noexcept
{
    *static_cast<State::Job*>(candidate) = {};
    return Status::Ok;
}
Status SceneDefinitionAdapter::Validate(void* context, const void* payload) noexcept
{
    if (context == payload)
    {
        return Status::InvalidHandle;
    }
    const auto* state = static_cast<SceneDefinitionAdapter*>(context)->mState;
    return state != nullptr && payload == state->Scene ? Status::Ok : Status::InvalidHandle;
}
} // namespace ludus::resources
