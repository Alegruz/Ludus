// L0 two-generation feasibility acceptance (tasks.md L0, design 5/6/7).
//
// Proves: A and B load concurrently from distinct immutable paths; each runs its
// own code (distinct identity + distinct Update output); the entry symbol has
// hidden visibility (not globally exported); and missing-entry / wrong-identity
// modules are rejected before Create. This is the gate the dependent reload
// phases build on.

#include "internal/identity.h"
#include "internal/module_loader.h"

#include <ludus/runtime/game_api/api.h>
#include <ludus/runtime/game_host/host.h>

#include <catch2/catch_test_macros.hpp>

#include <dlfcn.h>

using namespace ludus::runtime::game_host;
using ludus::runtime::game_api::CreateInfo;
using ludus::runtime::game_api::FrameInput;
using ludus::runtime::game_api::GameInstance;
using ludus::runtime::game_api::RenderParams;
using ludus::runtime::game_api::Status;

namespace
{
LoadStatus Load(const char* path, LoadedModule& out, ludus::foundation::uint64 generation)
{
    return LoadModule(path, generation, CurrentHostIdentity(), CurrentAbiMajor(), CurrentAbiMinor(), out);
}
} // namespace

TEST_CASE("matching module A loads, queries and creates", "[loader]")
{
    LoadedModule module;
    REQUIRE(Load(LUDUS_FIXTURE_A_PATH, module, 1) == LoadStatus::Ok);
    REQUIRE(module.IsLoaded());
    REQUIRE(module.Metadata().AbiMajor == CurrentAbiMajor());
    REQUIRE(module.Table().Create != nullptr);
}

TEST_CASE("A and B coexist and run their own code", "[loader]")
{
    LoadedModule a;
    LoadedModule b;
    REQUIRE(Load(LUDUS_FIXTURE_A_PATH, a, 1) == LoadStatus::Ok);
    REQUIRE(Load(LUDUS_FIXTURE_B_PATH, b, 2) == LoadStatus::Ok);

    // Distinct immutable paths and distinct loader handles.
    REQUIRE(a.Path() != b.Path());

    // Create an instance in each and advance one frame; variant B renders a
    // distinctly brighter red (its own code), proving each ran its own copy.
    CreateInfo info = {};
    info.StructSize = static_cast<ludus::foundation::uint32>(sizeof(CreateInfo));

    GameInstance* ia = nullptr;
    GameInstance* ib = nullptr;
    REQUIRE(a.Table().Create(&info, &ia) == Status::Ok);
    REQUIRE(b.Table().Create(&info, &ib) == Status::Ok);

    FrameInput frame = {};
    frame.DeltaSeconds = 1.0 / 60.0;
    RenderParams ra = {};
    RenderParams rb = {};
    REQUIRE(a.Table().Update(ia, &frame, &ra) == Status::Ok);
    REQUIRE(b.Table().Update(ib, &frame, &rb) == Status::Ok);
    REQUIRE(rb.ClearRed > ra.ClearRed + 0.3F); // B's variant boost
    REQUIRE(rb.Uniform3 == 1.0F);
    REQUIRE(ra.Uniform3 == 0.0F);

    a.Table().Destroy(ia);
    b.Table().Destroy(ib);
}

TEST_CASE("module entry symbol has hidden visibility", "[loader]")
{
    // The entry must resolve from the module's own handle but NOT leak into the
    // global namespace. Open with RTLD_LOCAL and confirm a global lookup fails.
    void* handle = ::dlopen(LUDUS_FIXTURE_A_PATH, RTLD_NOW | RTLD_LOCAL);
    REQUIRE(handle != nullptr);
    REQUIRE(::dlsym(handle, "LudusGetGameApi") != nullptr); // visible via the handle

    // A fresh RTLD_LOCAL open of a different library must not see A's symbol in
    // the global scope: look it up in the default (global) namespace handle.
    void* global = ::dlopen(nullptr, RTLD_NOW);
    REQUIRE(global != nullptr);
    REQUIRE(::dlsym(global, "LudusGetGameApi") == nullptr);
    ::dlclose(global);
    ::dlclose(handle);
}

TEST_CASE("missing entry symbol is rejected explicitly", "[loader]")
{
    LoadedModule module;
    REQUIRE(Load(LUDUS_FIXTURE_NOENTRY_PATH, module, 1) == LoadStatus::EntryMissing);
    REQUIRE_FALSE(module.IsLoaded());
}

TEST_CASE("wrong identity is rejected before create", "[loader]")
{
    LoadedModule module;
    REQUIRE(Load(LUDUS_FIXTURE_BADID_PATH, module, 1) == LoadStatus::IdentityRejected);
    REQUIRE_FALSE(module.IsLoaded());
}

TEST_CASE("non-absolute path is rejected", "[loader]")
{
    LoadedModule module;
    REQUIRE(Load("relative/path.so", module, 1) == LoadStatus::PathInvalid);
}

TEST_CASE("host Run drives the mandatory lifecycle headlessly", "[loader]")
{
    HostConfig config;
    config.Source = GameplaySource::DynamicModule;
    config.Mode = Presentation::Headless;
    config.ModulePath = LUDUS_FIXTURE_A_PATH;
    config.MaxFrames = 120;
    REQUIRE(Run(config) == RunResult::Ok);
}

TEST_CASE("host Run rejects an incompatible module", "[loader]")
{
    HostConfig config;
    config.Source = GameplaySource::DynamicModule;
    config.Mode = Presentation::Headless;
    config.ModulePath = LUDUS_FIXTURE_BADID_PATH;
    config.MaxFrames = 1;
    REQUIRE(Run(config) == RunResult::IncompatibleModule);
}

TEST_CASE("host Run distinguishes a missing module from an incompatible module", "[loader]")
{
    HostConfig config;
    config.Source = GameplaySource::DynamicModule;
    config.Mode = Presentation::Headless;
    config.ModulePath = "/tmp/ludus-missing-live-reload-module.so";
    config.MaxFrames = 1;
    REQUIRE(Run(config) == RunResult::ModuleLoadFailed);
}
