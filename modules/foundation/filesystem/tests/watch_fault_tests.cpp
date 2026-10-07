#include <ludus/foundation/filesystem/watch.hpp>

#include "internal/watch_test_hooks.hpp"
#include "persistence_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
using namespace ludus::foundation::filesystem;
namespace
{
int gAllocation = 0;
int gDenied = 0;
} // namespace
namespace ludus::foundation::filesystem::watch_test
{
bool AllocationAllowed() noexcept
{
    return ++gAllocation != gDenied;
}
} // namespace ludus::foundation::filesystem::watch_test
TEST_CASE("Watch storage allocation failure preserves root registrations and queued hints")
{
    filesystem_fixture::Fixture fixture;
    Watcher watcher;
    REQUIRE(watcher.Init(fixture.Root, {1, 2, 0}).Succeeded());
    WatchHandle handle;
    REQUIRE(watcher.Add("nested/data", 19, handle).Succeeded());
    fixture.Write("nested/data", "visible");
    REQUIRE(watcher.Advance(1, 1).Succeeded());
    for (int allocation = 1; allocation <= 3; ++allocation)
    {
        gAllocation = 0;
        gDenied = allocation;
        REQUIRE(watcher.Init(fixture.Root, {4, 4, 1}).Code == Status::OutOfMemory);
    }
    gDenied = 0;
    WatchHint hint;
    REQUIRE(watcher.Poll(hint).Available);
    REQUIRE(hint.Tag == 19);
    REQUIRE(hint.Handle == handle);
    REQUIRE(watcher.Remove(handle).Succeeded());
}
TEST_CASE("Watch registration observation polling and rescan use no storage allocation after Init")
{
    filesystem_fixture::Fixture fixture;
    Watcher watcher;
    REQUIRE(watcher.Init(fixture.Root, {2, 1, 0}).Succeeded());
    gAllocation = 0;
    gDenied = 1;
    WatchHandle a, b;
    REQUIRE(watcher.Add("nested/a", 1, a).Succeeded());
    REQUIRE(watcher.Add("nested/b", 2, b).Succeeded());
    fixture.Write("nested/a", "a");
    fixture.Write("nested/b", "b");
    REQUIRE(watcher.Advance(1, 2).Code == Status::LimitExceeded);
    WatchHint hint;
    REQUIRE(watcher.Poll(hint).Available);
    REQUIRE(watcher.BeginRescan().Succeeded());
    REQUIRE(watcher.NextRescan(hint).Available);
    REQUIRE(watcher.NextRescan(hint).Available);
    REQUIRE_FALSE(watcher.NextRescan(hint).Available);
    REQUIRE(watcher.Remove(a).Succeeded());
    REQUIRE(watcher.Remove(b).Succeeded());
    REQUIRE(gAllocation == 0);
    gDenied = 0;
}
