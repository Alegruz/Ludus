#pragma once

// Included by the existing test translation unit to avoid repeated Catch2 parsing.
namespace mesh_cases
{
using namespace ludus::foundation;
namespace r = ludus::resources;
namespace c = ludus::content;
namespace
{
c::ResourceId Id(const char* value)
{
    c::ResourceId id;
    REQUIRE(id.Set(value));
    return id;
}
struct Fixture final
{
    math::Vector3 Positions[260]{};
    math::Vector2 UV[260]{};
    math::Vector3 Normals[260]{};
    uint32 Indices[780]{};
    r::MeshSource Source;
    r::MeshDefinitionAdapter Adapter;
    c::Catalog Catalog;
    r::Manager Manager;
    r::ScopeHandle Scope;
    r::AssetHandle Asset;
    Fixture()
    {
        for (usize i = 0; i < 260; ++i)
        {
            Positions[i] = {static_cast<float32>(i), 0, 0};
            Normals[i] = {0, 0, 1};
        }
        Source.Resource.Id = Id("mesh/one");
        REQUIRE(Source.Resource.Path.Set("one.mesh"));
        Source.Resource.Type = c::Kind::Mesh;
        Source.Geometry = {Positions, 260, Indices, 780, UV, Normals};
        REQUIRE(Adapter.Initialize(&Source, 1) == r::Status::Ok);
        REQUIRE(Catalog.Put(Source.Resource) == c::Status::Ok);
        auto descriptor = Adapter.Descriptor();
        REQUIRE(Manager.Initialize(Catalog, &descriptor, 1) == r::Status::Ok);
        REQUIRE(Manager.OpenScope(Id("mesh/test"), Scope) == r::Status::Ok);
        REQUIRE(Manager.Resolve(Source.Resource.Id, c::Kind::Mesh, Asset) == r::Status::Ok);
    }
    ~Fixture()
    {
        (void)Manager.CloseScope(Scope);
        for (usize i = 0; i < 64 && Manager.Shutdown() != r::Status::Ok; ++i)
        {
            Manager.Service(24);
        }
    }
};
} // namespace
TEST_CASE("Mesh catalog kind round-trips without changing audio entries", "[resources][mesh]")
{
    c::Catalog catalog;
    c::Diagnostic diagnostic;
    REQUIRE(
        catalog.Read(
            R"({"version":1,"resources":[{"id":"m","kind":"mesh","path":"m.bin"},{"id":"s","kind":"sound","path":"s.json"}]})",
            diagnostic) == c::Status::Ok);
    c::Bytes bytes;
    REQUIRE(catalog.Write(bytes) == c::Status::Ok);
    c::Catalog roundtrip;
    REQUIRE(roundtrip.Read(bytes.String(), diagnostic) == c::Status::Ok);
    CHECK(roundtrip.Find("m")->Type == c::Kind::Mesh);
    CHECK(roundtrip.Find("s")->Type == c::Kind::Sound);
    CHECK(roundtrip.Read(R"({"version":1,"resources":[{"id":"x","kind":"meshlet","path":"x"}]})", diagnostic) ==
          c::Status::Invalid);
    CHECK(roundtrip.Find("m") != nullptr);
}
TEST_CASE("Mesh preparation is bounded and equal callers share one immutable CPU version", "[resources][mesh]")
{
    Fixture f;
    const auto peak = f.Adapter.Descriptor().Peak.ResidentBytes;
    r::RequestHandle a, b;
    REQUIRE(f.Manager.BeginAcquire(f.Scope, f.Asset, a) == r::Status::Ok);
    REQUIRE(f.Manager.BeginAcquire(f.Scope, f.Asset, b) == r::Status::Ok);
    CHECK(f.Manager.Inspect().ResidentBytes == peak);
    r::RequestInfo info;
    f.Manager.Service(1); // Begin only; no inline publication.
    REQUIRE(f.Manager.GetRequest(b, info) == r::Status::Ok);
    CHECK(info.State == r::RequestState::Working);
    CHECK(f.Adapter.Initialize(&f.Source, 1) == r::Status::InvalidState);
    f.Manager.Service(24); // One Poll visits at most 128/384 records.
    REQUIRE(f.Manager.GetRequest(b, info) == r::Status::Ok);
    CHECK(info.State == r::RequestState::Working);
    REQUIRE(f.Manager.Cancel(a) == r::Status::Ok);
    for (usize i = 0; i < 8; ++i)
    {
        f.Manager.Service(24);
    }
    r::Lease lease;
    REQUIRE(f.Manager.TakeLease(b, lease) == r::Status::Ok);
    const void* payload = nullptr;
    REQUIRE(f.Manager.Borrow(lease.Token(), c::Kind::Mesh, payload) == r::Status::Ok);
    const auto& mesh = *static_cast<const r::MeshDefinition*>(payload);
    CHECK(mesh.Positions != f.Positions);
    CHECK(mesh.Indices != f.Indices);
    CHECK(mesh.Texcoords != f.UV);
    CHECK(mesh.Normals != f.Normals);
    CHECK(mesh.Positions[259].X == 259);
    const void* unchanged = payload;
    CHECK(f.Manager.Borrow(lease.Token(), c::Kind::Sound, unchanged) == r::Status::InvalidHandle);
    CHECK(unchanged == payload);
    CHECK(f.Manager.CloseScope(f.Scope) == r::Status::Pending);
    REQUIRE(lease.Release() == r::Status::Ok);
    f.Manager.Service(24);
    CHECK(f.Manager.Inspect().ResidentBytes == 0);
    CHECK(f.Manager.Shutdown() == r::Status::Ok);
}
TEST_CASE("Final mesh cancellation releases partial CPU candidates without worker or GPU waits", "[resources][mesh]")
{
    Fixture f;
    r::RequestHandle request;
    REQUIRE(f.Manager.BeginAcquire(f.Scope, f.Asset, request) == r::Status::Ok);
    f.Manager.Service(24);
    REQUIRE(f.Manager.Cancel(request) == r::Status::Ok);
    CHECK(f.Manager.Inspect().ResidentBytes > 0);
    for (usize i = 0; i < 4; ++i)
    {
        f.Manager.Service(24);
    }
    CHECK(f.Manager.Inspect().ResidentBytes == 0);
    r::RequestInfo info;
    REQUIRE(f.Manager.GetRequest(request, info) == r::Status::Ok);
    CHECK(info.State == r::RequestState::Cancelled);
}
TEST_CASE("Mesh validation preserves drained profile and rejects malformed geometry", "[resources][mesh]")
{
    Fixture f;
    const auto epoch = f.Adapter.Descriptor().Epoch(&f.Adapter);
    math::Vector3 badPositions[260]{};
    math::Vector3 badNormals[260]{};
    uint32 badIndices[780]{};
    for (usize i = 0; i < 260; ++i)
    {
        badNormals[i] = {0, 0, 1};
    }
    auto valid = f.Source;
    valid.Geometry.Positions = badPositions;
    valid.Geometry.Normals = badNormals;
    valid.Geometry.Indices = badIndices;
    auto input = valid;
    input.Geometry.VertexCount = 4097;
    CHECK(f.Adapter.Initialize(&input, 1) == r::Status::Failed);
    input = valid;
    input.Geometry.IndexCount = 4;
    CHECK(f.Adapter.Initialize(&input, 1) == r::Status::Failed);
    input = valid;
    badIndices[0] = 260;
    CHECK(f.Adapter.Initialize(&input, 1) == r::Status::Failed);
    badIndices[0] = 0;
    badPositions[0].X = std::numeric_limits<float32>::infinity();
    CHECK(f.Adapter.Initialize(&input, 1) == r::Status::Failed);
    badPositions[0].X = 0;
    badNormals[0] = {};
    CHECK(f.Adapter.Initialize(&input, 1) == r::Status::Failed);
    badNormals[0] = {0, 0, 1};
    r::MeshSource duplicates[]{input, input};
    CHECK(f.Adapter.Initialize(duplicates, 2) == r::Status::Failed);
    CHECK(f.Adapter.Descriptor().Epoch(&f.Adapter) == epoch);
    REQUIRE(f.Manager.Shutdown() == r::Status::Ok);
    REQUIRE(f.Adapter.Initialize(&input, 1) == r::Status::Ok);
    CHECK(f.Adapter.Descriptor().Epoch(&f.Adapter) != epoch);
}
} // namespace mesh_cases
