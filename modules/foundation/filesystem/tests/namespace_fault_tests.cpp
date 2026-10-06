#include <ludus/foundation/filesystem/namespace.hpp>

#include "internal/namespace_test_hooks.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
using namespace ludus::foundation::filesystem;
namespace
{
usize gCalls = 0;
usize gFailAt = ~usize{0};
struct Injection final
{
    explicit Injection(usize index) noexcept
    {
        gCalls = 0;
        gFailAt = index;
    }
    ~Injection() noexcept
    {
        gFailAt = ~usize{0};
    }
};
} // namespace
namespace ludus::foundation::filesystem::test
{
bool NamespaceAllocationAllowed() noexcept
{
    return gCalls++ != gFailAt;
}
} // namespace ludus::foundation::filesystem::test

TEST_CASE("Every memory-provider allocation failure preserves an existing provider")
{
    const uint8 bytes[]{1, 2, 3};
    const MemoryEntry entries[]{{"first", bytes}, {"second", bytes}};
    ProviderHandle provider;
    const auto before = gCalls;
    REQUIRE(CreateMemoryProvider(entries, provider).Succeeded());
    const auto calls = gCalls - before;
    const auto* previous = provider.Get();
    REQUIRE(calls == 7);
    for (usize failure = 0; failure < calls; ++failure)
    {
        Result result;
        {
            const Injection fault(failure);
            result = CreateMemoryProvider(entries, provider);
        }
        REQUIRE(result.Code == Status::OutOfMemory);
        REQUIRE(provider.Get() == previous);
    }
    ProviderFile* file = nullptr;
    REQUIRE(provider.Get()->OpenRead("first", file).Succeeded());
    uint8 read[3]{};
    REQUIRE(file->ReadAt(0, read).BytesRead == 3);
    delete file;
}
TEST_CASE("Snapshot and opened-file allocation failures preserve previous revisions")
{
    const uint8 bytes[]{1};
    const MemoryEntry entry{"data", bytes};
    ProviderHandle provider;
    REQUIRE(CreateMemoryProvider({&entry, 1}, provider).Succeeded());
    const Mount mount{Root::Assets, {}, 0, 1, provider};
    MountSnapshot snapshot;
    REQUIRE(MountSnapshot::Create({&mount, 1}, 1, snapshot).Succeeded());
    VirtualPath path;
    REQUIRE(path.Set(Root::Assets, "data").Succeeded());
    VirtualFile file;
    REQUIRE(snapshot.OpenRead(path, file).Outcome.Succeeded());
    for (usize failure = 0; failure < 2; ++failure)
    {
        Result create;
        LookupResult open;
        {
            const Injection fault(failure);
            create = MountSnapshot::Create({&mount, 1}, 2, snapshot);
        }
        REQUIRE(create.Code == Status::OutOfMemory);
        REQUIRE(snapshot.Generation() == 1);
        {
            const Injection fault(failure);
            open = snapshot.OpenRead(path, file);
        }
        REQUIRE(open.Outcome.Code == Status::OutOfMemory);
        REQUIRE(file.Generation() == 1);
        REQUIRE(file.MountId() == 1);
        uint8 read = 0;
        REQUIRE(file.ReadAt(0, {&read, 1}).BytesRead == 1);
        REQUIRE(read == 1);
    }
}
TEST_CASE("Warm memory reads key validation and retained clones allocate nothing")
{
    const uint8 bytes[]{1, 2};
    const MemoryEntry entry{"data", bytes};
    ProviderHandle provider;
    REQUIRE(CreateMemoryProvider({&entry, 1}, provider).Succeeded());
    const Mount mount{Root::Assets, {}, 0, 1, provider};
    MountSnapshot snapshot;
    REQUIRE(MountSnapshot::Create({&mount, 1}, 1, snapshot).Succeeded());
    VirtualPath path;
    REQUIRE(path.Set(Root::Assets, "data").Succeeded());
    VirtualFile file, clone;
    REQUIRE(snapshot.OpenRead(path, file).Outcome.Succeeded());
    const auto before = gCalls;
    REQUIRE(path.Set(Root::Project, "nested/音.wav").Succeeded());
    auto retainedProvider = provider;
    auto retainedSnapshot = snapshot;
    REQUIRE(file.Clone(clone).Succeeded());
    uint8 read[2]{};
    REQUIRE(clone.ReadAt(0, read).BytesRead == 2);
    REQUIRE(clone.ReadAt(2, read).BytesRead == 0);
    REQUIRE(gCalls == before);
}
