// Isolated allocation replacement; exclude this executable under sanitizers.
#include <ludus/foundation/threading/jobs.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <new>

using namespace ludus::foundation;
using namespace ludus::foundation::threading;

namespace
{
std::atomic<uint32> gAllocations{0};
std::atomic<uint32> gFailAt{0};
std::atomic<bool> gTracking{false};
void* Allocate(usize bytes) noexcept
{
    if (gTracking.load())
    {
        const uint32 count = ++gAllocations;
        if (count == gFailAt.load())
        {
            return nullptr;
        }
    }
    return std::malloc(bytes == 0 ? 1 : bytes);
}
JobOutcome Increment(void* context) noexcept
{
    ++*static_cast<uint32*>(context);
    return JobOutcome::Succeeded;
}
} // namespace

void* operator new(usize bytes)
{
    if (auto* allocation = Allocate(bytes))
    {
        return allocation;
    }
    throw std::bad_alloc{};
}
void* operator new[](usize bytes)
{
    return ::operator new(bytes);
}
void* operator new(usize bytes, const std::nothrow_t&) noexcept
{
    return Allocate(bytes);
}
void* operator new[](usize bytes, const std::nothrow_t&) noexcept
{
    return Allocate(bytes);
}
void operator delete(void* allocation) noexcept
{
    std::free(allocation);
}
void operator delete[](void* allocation) noexcept
{
    std::free(allocation);
}
void operator delete(void* allocation, usize) noexcept
{
    std::free(allocation);
}
void operator delete[](void* allocation, usize) noexcept
{
    std::free(allocation);
}
void operator delete(void* allocation, const std::nothrow_t&) noexcept
{
    std::free(allocation);
}
void operator delete[](void* allocation, const std::nothrow_t&) noexcept
{
    std::free(allocation);
}

TEST_CASE("Every graph allocation failure cleans up and permits retry", "[threading][alloc]")
{
    for (const uint32 failAt : {1u, 2u, 3u, 4u})
    {
        JobGraph graph;
        gAllocations = 0;
        gFailAt = failAt;
        gTracking = true;
        const auto status = graph.Initialize({8, 8});
        gTracking = false;
        gFailAt = 0;
        REQUIRE(status == JobStatus::OutOfMemory);
        REQUIRE(graph.Initialize({8, 8}) == JobStatus::Ok);
    }
    JobSystem system;
    gAllocations = 0;
    gFailAt = 1;
    gTracking = true;
    const auto status = system.Initialize();
    gTracking = false;
    gFailAt = 0;
    REQUIRE(status == JobStatus::OutOfMemory);
    REQUIRE(system.Initialize() == JobStatus::Ok);
}

TEST_CASE("Graph build seal submit wait reset have zero scheduler allocations", "[threading][alloc]")
{
    for (const uint32 workers : {0u, 3u})
    {
        uint32 count = 0;
        JobGraph graph;
        JobSystem system;
        REQUIRE(graph.Initialize({2, 1}) == JobStatus::Ok);
        REQUIRE(system.Initialize({workers}) == JobStatus::Ok);
        // Warm the native worker/runtime path before tracking. Assertions stay
        // outside the measured region so Catch2 cannot affect the result.
        REQUIRE(graph.Seal() == JobStatus::Ok);
        REQUIRE(system.Submit(graph) == JobStatus::Ok);
        JobOutcome outcome{};
        REQUIRE(system.Wait(outcome) == JobStatus::Ok);
        REQUIRE(graph.Reset() == JobStatus::Ok);
        gAllocations = 0;
        gTracking = true;
        bool valid = true;
        for (uint32 iteration = 0; iteration < 100; ++iteration)
        {
            JobHandle first{};
            JobHandle second{};
            valid &= graph.Add(Increment, &count, first) == JobStatus::Ok;
            valid &= graph.Add(Increment, &count, second) == JobStatus::Ok;
            valid &= graph.DependsOn(second, first) == JobStatus::Ok;
            valid &= graph.Seal() == JobStatus::Ok;
            valid &= system.Submit(graph) == JobStatus::Ok;
            valid &= system.Wait(outcome) == JobStatus::Ok;
            valid &= outcome == JobOutcome::Succeeded;
            valid &= graph.Reset() == JobStatus::Ok;
        }
        gTracking = false;
        REQUIRE(valid);
        REQUIRE(gAllocations == 0);
        REQUIRE(count == 200);
    }
}
