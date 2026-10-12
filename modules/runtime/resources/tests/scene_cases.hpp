#pragma once

#include <ludus/runtime/resources/scene.h>

#include <cstring>

namespace
{
Array<uint8> SceneFixture(uint32 version = 1)
{
    Array<uint8> bytes;
    REQUIRE(bytes.TryResize(version == 2 ? 268 : 264));
    usize offset = 0;
    auto integer = [&](uint32 value) {
        for (usize i = 0; i < 4; ++i)
        {
            bytes[offset++] = static_cast<uint8>(value >> (i * 8));
        }
    };
    auto number = [&](float32 value) {
        uint32 bits = 0;
        std::memcpy(&bits, &value, 4);
        integer(bits);
    };
    std::memcpy(bytes.GetData(), "LUSCN01\0", 8);
    offset = 8;
    for (const uint32 value : {version, 1U, 1U, 0U, 1U})
    {
        integer(value);
    }
    for (const float32 value : {0.0F, 0.0F, 3.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F})
    {
        number(value);
    }
    integer(~uint32{0});
    if (version == 2)
    {
        integer(1);
    }
    for (const float32 value : {0.0F, .5F, 1.0F, 1.0F, 1.0F})
    {
        number(value);
    }
    integer(3);
    integer(3);
    for (const auto position : {ludus::foundation::math::Vector3{0, 0, 0},
                                ludus::foundation::math::Vector3{1, 0, 0},
                                ludus::foundation::math::Vector3{0, 1, 0}})
    {
        for (const float32 value : {position.X, position.Y, position.Z, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F})
        {
            number(value);
        }
    }
    integer(0);
    integer(1);
    integer(2);
    integer(0);
    integer(0);
    for (const float32 value : {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F})
    {
        number(value);
    }
    REQUIRE(offset == bytes.GetSize());
    return bytes;
}
} // namespace
TEST_CASE("Cooked scene decode owns arrays and malformed input preserves the last good definition",
          "[resources][scene]")
{
    auto bytes = SceneFixture();
    r::SceneDefinition scene;
    REQUIRE(r::SceneDefinition::Decode(bytes.GetData(), bytes.GetSize(), scene) == r::Status::Ok);
    REQUIRE(scene.Meshes.GetSize() == 1);
    REQUIRE(scene.Meshes[0].Positions[1].X == 1);
    REQUIRE(scene.Materials[0].Emission.X == 1);
    REQUIRE(r::SceneDefinition::Decode(bytes.GetData(), bytes.GetSize() - 1, scene) == r::Status::Failed);
    REQUIRE(scene.Meshes[0].Positions[1].X == 1);
    bytes[196] = 3; // An out-of-range mesh index.
    REQUIRE(r::SceneDefinition::Decode(bytes.GetData(), bytes.GetSize(), scene) == r::Status::Failed);
    REQUIRE(scene.Meshes[0].Indices[0] == 0);
    bytes = SceneFixture();
    bytes[232] = 0;
    bytes[233] = 0;
    bytes[234] = 0;
    bytes[235] = 0; // Singular second basis column.
    REQUIRE(r::SceneDefinition::Decode(bytes.GetData(), bytes.GetSize(), scene) == r::Status::Failed);
    bytes = SceneFixture();
    REQUIRE(bytes.TryAdd(0));
    REQUIRE(r::SceneDefinition::Decode(bytes.GetData(), bytes.GetSize(), scene) == r::Status::Failed);
}
TEST_CASE("Cooked scene manager requests share a definition with independent leases", "[resources][scene]")
{
    auto bytes = SceneFixture();
    r::SceneDefinition scene;
    REQUIRE(r::SceneDefinition::Decode(bytes.GetData(), bytes.GetSize(), scene) == r::Status::Ok);
    c::Resource resource;
    resource.Id = Id("scene/immutable");
    REQUIRE(resource.Path.Set("assets/scene.luscn"));
    resource.Type = c::Kind::CookedScene;
    c::Catalog catalog;
    REQUIRE(catalog.Put(resource) == c::Status::Ok);
    r::SceneDefinitionAdapter provider;
    REQUIRE(provider.Initialize(resource, scene) == r::Status::Ok);
    REQUIRE(provider.Initialize(resource, scene) == r::Status::InvalidState);
    const auto adapter = provider.Descriptor();
    r::Manager manager;
    REQUIRE(manager.Initialize(catalog, &adapter, 1) == r::Status::Ok);
    r::AssetHandle asset;
    r::ScopeHandle scope;
    REQUIRE(manager.Resolve(resource.Id, resource.Type, asset) == r::Status::Ok);
    REQUIRE(manager.OpenScope(Id("level"), scope) == r::Status::Ok);
    r::RequestHandle first, second;
    REQUIRE(manager.BeginAcquire(scope, asset, first) == r::Status::Ok);
    REQUIRE(manager.BeginAcquire(scope, asset, second) == r::Status::Ok);
    for (uint32 i = 0; i < 8; ++i)
    {
        manager.Service();
    }
    r::Lease one, two;
    REQUIRE(manager.TakeLease(first, one) == r::Status::Ok);
    REQUIRE(manager.TakeLease(second, two) == r::Status::Ok);
    const void* payload = nullptr;
    REQUIRE(manager.Borrow(one.Token(), resource.Type, payload) == r::Status::Ok);
    REQUIRE(payload == &scene);
    REQUIRE(one.Release() == r::Status::Ok);
    REQUIRE(manager.Borrow(two.Token(), resource.Type, payload) == r::Status::Ok);
    REQUIRE(manager.CloseScope(scope) == r::Status::Pending);
    REQUIRE(two.Release() == r::Status::Ok);
    for (uint32 i = 0; i < 8; ++i)
    {
        manager.Service();
    }
    REQUIRE(manager.Shutdown() == r::Status::Ok);
}

TEST_CASE("Cooked scene v2 preserves explicit sampler addressing and rejects unknown modes", "[resources][scene]")
{
    r::SceneDefinition scene;
    auto bytes = SceneFixture(2);
    REQUIRE(r::SceneDefinition::Decode(bytes.GetData(), bytes.GetSize(), scene) == r::Status::Ok);
    REQUIRE(scene.Materials[0].Repeat);
    bytes[72] = 2; // Unknown sampler mode in independently encoded v2 input.
    REQUIRE(r::SceneDefinition::Decode(bytes.GetData(), bytes.GetSize(), scene) == r::Status::Failed);
    REQUIRE(scene.Materials[0].Repeat);
    bytes[72] = 0;
    REQUIRE(r::SceneDefinition::Decode(bytes.GetData(), bytes.GetSize(), scene) == r::Status::Ok);
    REQUIRE_FALSE(scene.Materials[0].Repeat);
}
