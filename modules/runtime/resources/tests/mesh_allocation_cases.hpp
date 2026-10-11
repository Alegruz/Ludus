#pragma once

// Included by the existing test translation unit to avoid repeated Catch2 parsing.
namespace mesh_allocation_cases
{
TEST_CASE("Partial mesh allocation failure keeps reservation until cleanup and permits retry", "[resources][mesh]")
{
    using namespace ludus::foundation;
    namespace r = ludus::resources;
    namespace c = ludus::content;
    const math::Vector3 positions[]{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const math::Vector2 uv[]{{0, 0}, {1, 0}, {0, 1}};
    const math::Vector3 normals[]{{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    const uint32 indices[]{0, 1, 2};
    r::MeshSource source;
    source.Resource.Type = c::Kind::Mesh;
    REQUIRE(source.Resource.Id.Set("mesh/triangle"));
    REQUIRE(source.Resource.Path.Set("triangle.mesh"));
    source.Geometry = {positions, 3, indices, 3, uv, normals};
    r::MeshDefinitionAdapter adapter;
    REQUIRE(adapter.Initialize(&source, 1) == r::Status::Ok);
    c::Catalog catalog;
    REQUIRE(catalog.Put(source.Resource) == c::Status::Ok);
    const auto descriptor = adapter.Descriptor();
    r::Manager manager;
    REQUIRE(manager.Initialize(catalog, &descriptor, 1) == r::Status::Ok);
    struct Cleanup final
    {
        r::Manager& Manager;
        ~Cleanup()
        {
            r::internal::FailMeshArrayAllocationAfter(-1);
            while (Manager.Shutdown() != r::Status::Ok)
            {
                Manager.Service(24);
            }
        }
    } cleanup{manager};
    r::Lease lease;
    r::ScopeHandle scope;
    r::AssetHandle asset;
    REQUIRE(manager.OpenScope(source.Resource.Id, scope) == r::Status::Ok);
    REQUIRE(manager.Resolve(source.Resource.Id, c::Kind::Mesh, asset) == r::Status::Ok);
    r::RequestHandle failed;
    REQUIRE(manager.BeginAcquire(scope, asset, failed) == r::Status::Ok);
    r::internal::FailMeshArrayAllocationAfter(GENERATE(0, 1, 2, 3));
    manager.Service(1); // Begin fails after potentially owning earlier arrays.
    r::internal::FailMeshArrayAllocationAfter(-1);
    r::RequestInfo result;
    REQUIRE(manager.GetRequest(failed, result) == r::Status::Ok);
    CHECK(result.State == r::RequestState::Failed);
    CHECK(result.Result == r::Status::OutOfMemory);
    CHECK(manager.Inspect().ResidentBytes == descriptor.Peak.ResidentBytes);
    manager.Service(24);
    CHECK(manager.Inspect().ResidentBytes == 0);
    REQUIRE(manager.GetRequest(failed, result) == r::Status::Ok);
    CHECK(result.Result == r::Status::OutOfMemory); // Durable after physical cleanup.
    r::RequestHandle retry;
    REQUIRE(manager.BeginAcquire(scope, asset, retry) == r::Status::Ok);
    for (usize turn = 0; turn < 4; ++turn)
    {
        manager.Service(24);
    }
    REQUIRE(manager.TakeLease(retry, lease) == r::Status::Ok);
    const void* payload = nullptr;
    REQUIRE(manager.Borrow(lease.Token(), c::Kind::Mesh, payload) == r::Status::Ok);
    CHECK(static_cast<const r::MeshDefinition*>(payload)->Normals[0].Z == 1);
}
} // namespace mesh_allocation_cases
