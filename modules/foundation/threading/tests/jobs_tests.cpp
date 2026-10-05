#include <ludus/foundation/threading/jobs.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace ludus::foundation;
using namespace ludus::foundation::threading;

namespace
{
JobOutcome Increment(void* context) noexcept
{
    ++*static_cast<uint32*>(context);
    return JobOutcome::Succeeded;
}
JobOutcome Fail(void*) noexcept
{
    return JobOutcome::Failed;
}
JobOutcome InvalidResult(void*) noexcept
{
    return JobOutcome::Running;
}
JobOutcome Succeed(void*) noexcept
{
    return JobOutcome::Succeeded;
}
JobOutcome Cancelled(void*) noexcept
{
    return JobOutcome::Cancelled;
}

struct Inputs
{
    uint32 Left = 0;
    uint32 Right = 0;
    uint32 Sum = 0;
};
JobOutcome Sum(void* context) noexcept
{
    auto& inputs = *static_cast<Inputs*>(context);
    inputs.Sum = inputs.Left + inputs.Right;
    return inputs.Sum == 2 ? JobOutcome::Succeeded : JobOutcome::Failed;
}
struct HeldJob
{
    std::atomic<bool> Entered{false};
    std::atomic<bool> Release{false};
};
JobOutcome Hold(void* context) noexcept
{
    auto& held = *static_cast<HeldJob*>(context);
    held.Entered.store(true);
    held.Entered.notify_all();
    held.Release.wait(false);
    return JobOutcome::Succeeded;
}
struct Reentrant
{
    JobSystem* System = nullptr;
    JobGraph* Graph = nullptr;
    JobStatus WaitStatus = JobStatus::Ok;
    JobStatus SubmitStatus = JobStatus::Ok;
    JobStatus ShutdownStatus = JobStatus::Ok;
};
JobOutcome Reenter(void* context) noexcept
{
    auto& data = *static_cast<Reentrant*>(context);
    JobOutcome outcome{};
    data.WaitStatus = data.System->Wait(outcome);
    data.SubmitStatus = data.System->Submit(*data.Graph);
    data.ShutdownStatus = data.System->Shutdown();
    return JobOutcome::Succeeded;
}
JobOutcome Outcome(JobGraph& graph, JobHandle handle)
{
    JobOutcome outcome{};
    REQUIRE(graph.GetOutcome(handle, outcome) == JobStatus::Ok);
    return outcome;
}
} // namespace

TEST_CASE("Graph construction validates handles, capacity and cycles", "[threading]")
{
    JobGraph graph;
    JobHandle first{};
    JobHandle second{};
    JobHandle third{};
    REQUIRE(graph.Add(Succeed, nullptr, first) == JobStatus::NotInitialized);
    REQUIRE(graph.Initialize({0, 0}) == JobStatus::InvalidArgument);
    REQUIRE(graph.Initialize({2, 2}) == JobStatus::Ok);
    REQUIRE(graph.Initialize() == JobStatus::AlreadyInitialized);
    REQUIRE(graph.Add(nullptr, nullptr, first) == JobStatus::InvalidArgument);
    REQUIRE(graph.Add(Succeed, nullptr, first) == JobStatus::Ok);
    REQUIRE(graph.Add(Succeed, nullptr, second) == JobStatus::Ok);
    REQUIRE(graph.Add(Succeed, nullptr, third) == JobStatus::CapacityExceeded);
    REQUIRE(third.GraphId == 0);
    REQUIRE(graph.DependsOn(first, first) == JobStatus::CycleDetected);
    REQUIRE(graph.DependsOn(second, first) == JobStatus::Ok);
    REQUIRE(graph.DependsOn(second, first) == JobStatus::DuplicateDependency);
    REQUIRE(graph.DependsOn(first, second) == JobStatus::Ok);
    REQUIRE(graph.Seal() == JobStatus::CycleDetected);
    JobSystem system;
    REQUIRE(system.Initialize() == JobStatus::Ok);
    REQUIRE(system.Submit(graph) == JobStatus::NotSealed);
    REQUIRE(graph.Reset() == JobStatus::Ok);
    REQUIRE(graph.DependsOn(second, first) == JobStatus::InvalidHandle);
    JobOutcome result{};
    REQUIRE(graph.GetOutcome(first, result) == JobStatus::InvalidHandle);

    JobGraph other;
    REQUIRE(other.Initialize({1, 0}) == JobStatus::Ok);
    REQUIRE(other.Add(Succeed, nullptr, third) == JobStatus::Ok);
    REQUIRE(graph.Add(Succeed, nullptr, first) == JobStatus::Ok);
    REQUIRE(graph.DependsOn(first, third) == JobStatus::InvalidHandle);
    REQUIRE(graph.Add(Succeed, nullptr, second) == JobStatus::Ok);
    REQUIRE(graph.Seal() == JobStatus::Ok);
    REQUIRE(graph.Add(Succeed, nullptr, third) == JobStatus::Busy);
}

TEST_CASE("Dependency publication and graph reuse agree across serial and workers", "[threading]")
{
    for (const uint32 workers : {0u, 1u, 4u})
    {
        JobSystem system;
        REQUIRE(system.Initialize({workers}) == JobStatus::Ok);
        JobGraph graph;
        REQUIRE(graph.Initialize({3, 2}) == JobStatus::Ok);
        Inputs inputs;
        JobHandle left{};
        JobHandle right{};
        JobHandle sum{};
        REQUIRE(graph.Add(Increment, &inputs.Left, left) == JobStatus::Ok);
        REQUIRE(graph.Add(Increment, &inputs.Right, right) == JobStatus::Ok);
        REQUIRE(graph.Add(Sum, &inputs, sum) == JobStatus::Ok);
        REQUIRE(graph.DependsOn(sum, left) == JobStatus::Ok);
        REQUIRE(graph.DependsOn(sum, right) == JobStatus::Ok);
        REQUIRE(graph.Seal() == JobStatus::Ok);
        for (uint32 iteration = 0; iteration < 100; ++iteration)
        {
            inputs = {};
            REQUIRE(system.Submit(graph) == JobStatus::Ok);
            REQUIRE(graph.Reset() == JobStatus::Busy);
            REQUIRE(system.Submit(graph) == JobStatus::Busy);
            JobOutcome result{};
            REQUIRE(system.Wait(result, iteration % 2 == 0) == JobStatus::Ok);
            REQUIRE(result == JobOutcome::Succeeded);
            REQUIRE(inputs.Sum == 2);
            REQUIRE(Outcome(graph, sum) == JobOutcome::Succeeded);
        }
    }
}

TEST_CASE("Failure and cancellation block dependent callbacks without losing independent work", "[threading]")
{
    for (const auto failure : {Fail, InvalidResult, Cancelled})
    {
        for (const uint32 workers : {0u, 3u})
        {
            JobSystem system;
            REQUIRE(system.Initialize({workers}) == JobStatus::Ok);
            JobGraph graph;
            REQUIRE(graph.Initialize({4, 2}) == JobStatus::Ok);
            uint32 calls = 0;
            uint32 independentCalls = 0;
            JobHandle failed{};
            JobHandle child{};
            JobHandle grandchild{};
            JobHandle independent{};
            REQUIRE(graph.Add(failure, nullptr, failed) == JobStatus::Ok);
            REQUIRE(graph.Add(Increment, &calls, child) == JobStatus::Ok);
            REQUIRE(graph.Add(Increment, &calls, grandchild) == JobStatus::Ok);
            REQUIRE(graph.Add(Increment, &independentCalls, independent) == JobStatus::Ok);
            REQUIRE(graph.DependsOn(child, failed) == JobStatus::Ok);
            REQUIRE(graph.DependsOn(grandchild, child) == JobStatus::Ok);
            REQUIRE(graph.Seal() == JobStatus::Ok);
            REQUIRE(system.Submit(graph) == JobStatus::Ok);
            JobOutcome result{};
            REQUIRE(system.Wait(result) == JobStatus::Ok);
            REQUIRE(result == (failure == Cancelled ? JobOutcome::Cancelled : JobOutcome::Failed));
            REQUIRE(Outcome(graph, child) == JobOutcome::Blocked);
            REQUIRE(Outcome(graph, grandchild) == JobOutcome::Blocked);
            REQUIRE(calls == 0);
            REQUIRE(independentCalls == 1);
        }
    }
}

TEST_CASE("Cancellation skips queued work and preserves a running callback", "[threading]")
{
    HeldJob held;
    uint32 calls = 0;
    JobGraph graph;
    JobSystem system;
    REQUIRE(graph.Initialize({3, 1}) == JobStatus::Ok);
    REQUIRE(system.Initialize({1}) == JobStatus::Ok);
    JobHandle running{};
    JobHandle queued{};
    JobHandle dependent{};
    REQUIRE(graph.Add(Hold, &held, running) == JobStatus::Ok);
    REQUIRE(graph.Add(Increment, &calls, queued) == JobStatus::Ok);
    REQUIRE(graph.Add(Increment, &calls, dependent) == JobStatus::Ok);
    REQUIRE(graph.DependsOn(dependent, running) == JobStatus::Ok);
    REQUIRE(graph.Seal() == JobStatus::Ok);
    REQUIRE(system.Submit(graph) == JobStatus::Ok);
    held.Entered.wait(false);
    JobSnapshot snapshot{};
    REQUIRE(system.Snapshot(snapshot) == JobStatus::Ok);
    REQUIRE(snapshot.Running == 1);
    REQUIRE(snapshot.Pending == 2);
    REQUIRE(system.Cancel() == JobStatus::Ok);
    held.Release.store(true);
    held.Release.notify_all();
    JobOutcome result{};
    REQUIRE(system.Wait(result) == JobStatus::Ok);
    REQUIRE(result == JobOutcome::Cancelled);
    REQUIRE(Outcome(graph, running) == JobOutcome::Succeeded);
    REQUIRE(Outcome(graph, queued) == JobOutcome::Cancelled);
    REQUIRE(Outcome(graph, dependent) == JobOutcome::Cancelled);
    REQUIRE(calls == 0);
}

TEST_CASE("Callbacks cannot recursively wait, submit or shut down", "[threading]")
{
    for (const uint32 workers : {0u, 2u})
    {
        JobSystem system;
        JobGraph graph;
        REQUIRE(system.Initialize({workers}) == JobStatus::Ok);
        REQUIRE(graph.Initialize({1, 0}) == JobStatus::Ok);
        Reentrant data{&system, &graph};
        JobHandle handle{};
        REQUIRE(graph.Add(Reenter, &data, handle) == JobStatus::Ok);
        REQUIRE(graph.Seal() == JobStatus::Ok);
        REQUIRE(system.Submit(graph) == JobStatus::Ok);
        JobOutcome result{};
        REQUIRE(system.Wait(result) == JobStatus::Ok);
        REQUIRE(data.WaitStatus == JobStatus::InsideJob);
        REQUIRE(data.SubmitStatus == JobStatus::InsideJob);
        REQUIRE(data.ShutdownStatus == JobStatus::InsideJob);
    }
}

TEST_CASE("Shutdown drains accepted work and supports repeated initialization", "[threading]")
{
    JobGraph graph;
    REQUIRE(graph.Initialize({512, 0}) == JobStatus::Ok);
    std::vector<uint32> calls(512, 0);
    JobHandle handle{};
    for (auto& count : calls)
    {
        REQUIRE(graph.Add(Increment, &count, handle) == JobStatus::Ok);
    }
    REQUIRE(graph.Seal() == JobStatus::Ok);
    JobSystem system;
    for (uint32 repeat = 0; repeat < 10; ++repeat)
    {
        REQUIRE(system.Initialize({4}) == JobStatus::Ok);
        REQUIRE(system.Submit(graph) == JobStatus::Ok);
        REQUIRE(system.Shutdown() == JobStatus::Ok);
        for (const auto count : calls)
        {
            REQUIRE(count == repeat + 1);
        }
        REQUIRE(system.Shutdown() == JobStatus::Ok);
    }
    REQUIRE(graph.Reset() == JobStatus::Ok);
}

TEST_CASE("Empty graphs and invalid system operations have explicit results", "[threading]")
{
    JobSystem system;
    JobGraph graph;
    JobOutcome result{};
    REQUIRE(system.Wait(result) == JobStatus::NotInitialized);
    REQUIRE(system.Initialize({65}) == JobStatus::InvalidArgument);
    REQUIRE(system.Initialize() == JobStatus::Ok);
    REQUIRE(system.Initialize() == JobStatus::AlreadyInitialized);
    REQUIRE(system.Wait(result) == JobStatus::InvalidArgument);
    REQUIRE(system.Cancel() == JobStatus::InvalidArgument);
    REQUIRE(system.Submit(graph) == JobStatus::NotInitialized);
    REQUIRE(graph.Initialize({1, 0}) == JobStatus::Ok);
    REQUIRE(graph.Seal() == JobStatus::Ok);
    REQUIRE(system.Submit(graph) == JobStatus::Ok);
    REQUIRE(system.Wait(result, false) == JobStatus::Ok);
    REQUIRE(result == JobOutcome::Succeeded);
}

TEST_CASE("A graph cannot be admitted by two pools and graph destruction drains work", "[threading]")
{
    uint32 count = 0;
    JobSystem first;
    JobSystem second;
    REQUIRE(first.Initialize({2}) == JobStatus::Ok);
    REQUIRE(second.Initialize({2}) == JobStatus::Ok);
    JobHandle stale{};
    {
        JobGraph graph;
        REQUIRE(graph.Initialize({1, 0}) == JobStatus::Ok);
        REQUIRE(graph.Add(Increment, &count, stale) == JobStatus::Ok);
        REQUIRE(graph.Seal() == JobStatus::Ok);
        REQUIRE(first.Submit(graph) == JobStatus::Ok);
        REQUIRE(second.Submit(graph) == JobStatus::Busy);
    }
    REQUIRE(count == 1);
    JobGraph graph;
    REQUIRE(graph.Initialize({1, 0}) == JobStatus::Ok);
    JobHandle fresh{};
    REQUIRE(graph.Add(Succeed, nullptr, fresh) == JobStatus::Ok);
    JobOutcome result{};
    REQUIRE(graph.GetOutcome(stale, result) == JobStatus::InvalidHandle);
}

namespace
{
struct FanIn
{
    uint32* Values = nullptr;
    uint32 Count = 0;
    uint32 Total = 0;
};
JobOutcome Reduce(void* context) noexcept
{
    auto& data = *static_cast<FanIn*>(context);
    data.Total = 0;
    for (uint32 index = 0; index < data.Count; ++index)
    {
        data.Total += data.Values[index];
    }
    return data.Total == data.Count ? JobOutcome::Succeeded : JobOutcome::Failed;
}
} // namespace

TEST_CASE("Wide fan-out and fan-in run exactly once across repeated sleeping workers", "[threading]")
{
    constexpr uint32 WIDTH = 128;
    uint32 values[WIDTH]{};
    FanIn data{values, WIDTH, 0};
    JobGraph graph;
    JobSystem system;
    REQUIRE(graph.Initialize({WIDTH + 2, WIDTH * 2}) == JobStatus::Ok);
    REQUIRE(system.Initialize({4}) == JobStatus::Ok);
    JobHandle root{};
    JobHandle join{};
    REQUIRE(graph.Add(Succeed, nullptr, root) == JobStatus::Ok);
    REQUIRE(graph.Add(Reduce, &data, join) == JobStatus::Ok);
    for (uint32 index = 0; index < WIDTH; ++index)
    {
        JobHandle child{};
        REQUIRE(graph.Add(Increment, &values[index], child) == JobStatus::Ok);
        REQUIRE(graph.DependsOn(child, root) == JobStatus::Ok);
        REQUIRE(graph.DependsOn(join, child) == JobStatus::Ok);
    }
    REQUIRE(graph.Seal() == JobStatus::Ok);
    for (uint32 repeat = 0; repeat < 100; ++repeat)
    {
        for (auto& value : values)
        {
            value = 0;
        }
        REQUIRE(system.Submit(graph) == JobStatus::Ok);
        // Force repeated full completion notifications to a non-helping owner.
        JobOutcome outcome{};
        REQUIRE(system.Wait(outcome, false) == JobStatus::Ok);
        REQUIRE(outcome == JobOutcome::Succeeded);
        REQUIRE(data.Total == WIDTH);
    }
}
