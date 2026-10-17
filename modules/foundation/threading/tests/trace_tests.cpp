// Trace and worker hook contracts of JobSystemHooks (milestone T2).
#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/threading/jobs.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>

using namespace ludus::foundation;
using namespace ludus::foundation::threading;

namespace
{
constexpr usize MAX_RECORDED_EVENTS = 256;
constexpr uint32 UNSET = static_cast<uint32>(-1);

std::atomic<uint32> gNextThreadOrdinal{1};
uint32 GetThreadOrdinal() noexcept
{
    thread_local const uint32 THREAD_ORDINAL = gNextThreadOrdinal.fetch_add(1);
    return THREAD_ORDINAL;
}

struct RecordedEvent
{
    JobTraceEvent Event{};
    uint32 ThreadOrdinal = 0;
};
struct TraceLog
{
    StaticArray<RecordedEvent, MAX_RECORDED_EVENTS> Events{};
    std::atomic<uint32> Count{0};
    std::atomic<uint32> FilterCalls{0};
    bool ShouldTrace = true;

    [[nodiscard]] uint32 GetCount() const noexcept
    {
        const uint32 count = Count.load();
        return count < MAX_RECORDED_EVENTS ? count : static_cast<uint32>(MAX_RECORDED_EVENTS);
    }
    // Index of the only event of this kind for this job, or UNSET.
    [[nodiscard]] uint32 Find(JobTraceKind kind, uint32 jobIndex) const noexcept
    {
        uint32 found = UNSET;
        for (uint32 i = 0; i < GetCount(); ++i)
        {
            if (Events[i].Event.Kind == kind && Events[i].Event.JobIndex == jobIndex)
            {
                if (found != UNSET)
                {
                    return UNSET;
                }
                found = i;
            }
        }
        return found;
    }
    [[nodiscard]] uint32 CountKind(JobTraceKind kind) const noexcept
    {
        uint32 count = 0;
        for (uint32 i = 0; i < GetCount(); ++i)
        {
            count += Events[i].Event.Kind == kind ? 1 : 0;
        }
        return count;
    }
};

void RecordTrace(void* context, const JobTraceEvent& event) noexcept
{
    TraceLog& log = *static_cast<TraceLog*>(context);
    // Slot order follows happens-before: the atomic RMWs share one modification order.
    const uint32 slot = log.Count.fetch_add(1);
    if (slot < MAX_RECORDED_EVENTS)
    {
        log.Events[slot] = { .Event = event, .ThreadOrdinal = GetThreadOrdinal() };
    }
}
bool FilterTrace(void* context) noexcept
{
    TraceLog& log = *static_cast<TraceLog*>(context);
    ++log.FilterCalls;
    return log.ShouldTrace;
}
JobSystemHooks MakeHooks(TraceLog& inoutLog) noexcept
{
    return { .TraceOrNull = RecordTrace, .ShouldTraceOrNull = FilterTrace, .ContextOrNull = &inoutLog };
}

JobOutcome Increment(void* context) noexcept
{
    ++*static_cast<uint32*>(context);
    return JobOutcome::Succeeded;
}
// Sibling jobs run concurrently, so a shared call counter must be atomic.
JobOutcome CountCall(void* context) noexcept
{
    ++*static_cast<std::atomic<uint32>*>(context);
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

constexpr JobLabel LABEL_LEFT{ .NameOrNull = "Left", .SiteId = 11 };
constexpr JobLabel LABEL_RIGHT{ .NameOrNull = "Right", .SiteId = 12 };
constexpr JobLabel LABEL_JOIN{ .NameOrNull = "Join", .SiteId = 13 };
constexpr JobLabel LABEL_TAIL{ .NameOrNull = "Tail", .SiteId = 14 };

struct WorkerHookLog
{
    JobSystem* SystemOrNull = nullptr;
    std::atomic<uint32> Starts{0};
    std::atomic<uint32> Stops{0};
    std::atomic<uint32> StartedMask{0};
    std::atomic<uint32> CallerStarts{0};
    std::atomic<uint32> InsideJobResults{0};
    uint32 CallerOrdinal = 0;
};
void StartWorker(void* context, uint32 workerIndex) noexcept
{
    WorkerHookLog& log = *static_cast<WorkerHookLog*>(context);
    log.StartedMask.fetch_or(uint32{1} << workerIndex);
    log.CallerStarts += GetThreadOrdinal() == log.CallerOrdinal ? 1 : 0;
    JobOutcome outcome{};
    // Lifecycle calls from a hook are rejected exactly as from a callback.
    log.InsideJobResults += log.SystemOrNull->Wait(outcome) == JobStatus::InsideJob ? 1 : 0;
    ++log.Starts;
}
void StopWorker(void* context, uint32) noexcept
{
    ++static_cast<WorkerHookLog*>(context)->Stops;
}
} // namespace

TEST_CASE("Trace hooks report each executed job around its callback with its dispatch edge", "[threading][trace]")
{
    for (const uint32 workers : {0u, 1u, 4u})
    {
        TraceLog log;
        JobSystem system;
        REQUIRE(system.Initialize({ .WorkerCount = workers, .Hooks = MakeHooks(log) }) == JobStatus::Ok);
        JobGraph graph;
        REQUIRE(graph.Initialize({ .MaxJobs = 5, .MaxDependencies = 3 }) == JobStatus::Ok);
        std::atomic<uint32> calls{0};
        JobHandle left{};
        JobHandle right{};
        JobHandle join{};
        JobHandle tail{};
        JobHandle anonymous{};
        REQUIRE(graph.Add(CountCall, &calls, left, LABEL_LEFT) == JobStatus::Ok);
        REQUIRE(graph.Add(CountCall, &calls, right, LABEL_RIGHT) == JobStatus::Ok);
        REQUIRE(graph.Add(CountCall, &calls, join, LABEL_JOIN) == JobStatus::Ok);
        REQUIRE(graph.Add(CountCall, &calls, tail, LABEL_TAIL) == JobStatus::Ok);
        REQUIRE(graph.Add(CountCall, &calls, anonymous) == JobStatus::Ok);
        REQUIRE(graph.DependsOn(join, left) == JobStatus::Ok);  // edge 0
        REQUIRE(graph.DependsOn(join, right) == JobStatus::Ok); // edge 1
        REQUIRE(graph.DependsOn(tail, join) == JobStatus::Ok);  // edge 2
        REQUIRE(graph.Seal() == JobStatus::Ok);
        for (uint32 iteration = 0; iteration < 20; ++iteration)
        {
            log.Count = 0;
            REQUIRE(system.Submit(graph) == JobStatus::Ok);
            JobOutcome outcome{};
            REQUIRE(system.Wait(outcome, iteration % 2 == 0) == JobStatus::Ok);
            REQUIRE(outcome == JobOutcome::Succeeded);
            // Every report for the submission has returned before Wait returns.
            const uint32 total = log.Count.load();
            REQUIRE(total == 1 + 5 * 2 + 3 + 2);
            REQUIRE(log.GetCount() == total);

            const JobTraceEvent& submitted = log.Events[0].Event;
            REQUIRE(submitted.Kind == JobTraceKind::Submitted);
            REQUIRE(submitted.SubmissionId != 0);
            REQUIRE(submitted.GraphId == left.GraphId);
            REQUIRE(submitted.ReadyDepth == 3);
            REQUIRE(submitted.JobCount == 5);
            REQUIRE(log.Events[total - 1].Event.Kind == JobTraceKind::WaitReturned);
            REQUIRE(log.Events[total - 1].Event.Outcome == JobOutcome::Succeeded);
            REQUIRE(log.CountKind(JobTraceKind::WaitEntered) == 1);

            uint32 sequenceMask = 0;
            for (const JobHandle handle : {left, right, join, tail, anonymous})
            {
                const uint32 started = log.Find(JobTraceKind::Started, handle.Index);
                const uint32 finished = log.Find(JobTraceKind::Finished, handle.Index);
                REQUIRE(started != UNSET);
                REQUIRE(finished != UNSET);
                REQUIRE(started < finished);
                const RecordedEvent& start = log.Events[started];
                const RecordedEvent& finish = log.Events[finished];
                REQUIRE(start.ThreadOrdinal == finish.ThreadOrdinal);
                REQUIRE(start.Event.WorkerIndex == finish.Event.WorkerIndex);
                REQUIRE((start.Event.WorkerIndex < workers || start.Event.WorkerIndex == CALLING_THREAD_WORKER_INDEX));
                REQUIRE(start.Event.SubmissionId == submitted.SubmissionId);
                REQUIRE(start.Event.Sequence == finish.Event.Sequence);
                REQUIRE(finish.Event.Outcome == JobOutcome::Succeeded);
                sequenceMask |= uint32{1} << start.Event.Sequence;
                // Signalled reports sit between Started and Finished on the executing thread.
                for (uint32 i = started + 1; i < finished; ++i)
                {
                    if (log.Events[i].Event.Kind == JobTraceKind::Signalled &&
                        log.Events[i].Event.JobIndex == handle.Index)
                    {
                        REQUIRE(log.Events[i].ThreadOrdinal == start.ThreadOrdinal);
                    }
                }
            }
            REQUIRE(sequenceMask == 0b11111);
            // Roots are ready first, in Add order, with no dispatch edge.
            REQUIRE(log.Events[log.Find(JobTraceKind::Started, left.Index)].Event.Sequence == 0);
            REQUIRE(log.Events[log.Find(JobTraceKind::Started, right.Index)].Event.Sequence == 1);
            REQUIRE(log.Events[log.Find(JobTraceKind::Started, anonymous.Index)].Event.Sequence == 2);
            REQUIRE(log.Events[log.Find(JobTraceKind::Started, left.Index)].Event.EdgeIndex == NO_JOB_EDGE);
            REQUIRE(log.Events[log.Find(JobTraceKind::Started, left.Index)].Event.Label.NameOrNull ==
                    LABEL_LEFT.NameOrNull);
            REQUIRE(log.Events[log.Find(JobTraceKind::Started, anonymous.Index)].Event.Label.SiteId == 0);
            // A dependent starts from the edge whose signal made it ready, reported earlier.
            const JobTraceEvent& joinStart = log.Events[log.Find(JobTraceKind::Started, join.Index)].Event;
            REQUIRE(joinStart.Label.SiteId == LABEL_JOIN.SiteId);
            REQUIRE((joinStart.EdgeIndex == 0 || joinStart.EdgeIndex == 1));
            REQUIRE(log.Events[log.Find(JobTraceKind::Started, tail.Index)].Event.EdgeIndex == 2);
            uint32 signals = 0;
            for (uint32 i = 0; i < total; ++i)
            {
                const JobTraceEvent& event = log.Events[i].Event;
                if (event.Kind != JobTraceKind::Signalled)
                {
                    continue;
                }
                ++signals;
                const JobHandle dependent = event.EdgeIndex == 2 ? tail : join;
                REQUIRE(event.DependentIndex == dependent.Index);
                REQUIRE(i < log.Find(JobTraceKind::Started, dependent.Index));
            }
            REQUIRE(signals == 3);
        }
        REQUIRE(calls == 100);
        REQUIRE(log.FilterCalls == 20);
    }
}

TEST_CASE("The serial executor reports one deterministic trace order", "[threading][trace]")
{
    TraceLog log;
    JobSystem system;
    REQUIRE(system.Initialize({ .Hooks = MakeHooks(log) }) == JobStatus::Ok);
    JobGraph graph;
    REQUIRE(graph.Initialize({ .MaxJobs = 2, .MaxDependencies = 1 }) == JobStatus::Ok);
    uint32 calls = 0;
    JobHandle first{};
    JobHandle second{};
    REQUIRE(graph.Add(Increment, &calls, first, LABEL_LEFT) == JobStatus::Ok);
    REQUIRE(graph.Add(Increment, &calls, second, LABEL_RIGHT) == JobStatus::Ok);
    REQUIRE(graph.DependsOn(second, first) == JobStatus::Ok);
    REQUIRE(graph.Seal() == JobStatus::Ok);
    REQUIRE(system.Submit(graph) == JobStatus::Ok);
    JobOutcome outcome{};
    REQUIRE(system.Wait(outcome) == JobStatus::Ok);
    constexpr usize EXPECTED_EVENTS = 8;
    const StaticArray<JobTraceKind, EXPECTED_EVENTS> expected = {
        JobTraceKind::Submitted,
        JobTraceKind::Started,
        JobTraceKind::Signalled,
        JobTraceKind::Finished,
        JobTraceKind::Started,
        JobTraceKind::Finished,
        JobTraceKind::WaitEntered,
        JobTraceKind::WaitReturned,
    };
    REQUIRE(log.GetCount() == EXPECTED_EVENTS);
    for (uint32 i = 0; i < EXPECTED_EVENTS; ++i)
    {
        REQUIRE(log.Events[i].Event.Kind == expected[i]);
        REQUIRE(log.Events[i].Event.WorkerIndex == CALLING_THREAD_WORKER_INDEX);
    }
    REQUIRE(log.Events[1].Event.ReadyDepth == 0);
    REQUIRE(log.Events[2].Event.EdgeIndex == 0);
    REQUIRE(log.Events[2].Event.DependentIndex == second.Index);
    REQUIRE(log.Events[4].Event.EdgeIndex == 0);
    REQUIRE(log.Events[4].Event.Sequence == 1);
}

TEST_CASE("Failed jobs signal no edges and skipped jobs are not reported", "[threading][trace]")
{
    for (const JobFunction failure : {Fail, InvalidResult})
    {
        for (const uint32 workers : {0u, 2u})
        {
            TraceLog log;
            JobSystem system;
            REQUIRE(system.Initialize({ .WorkerCount = workers, .Hooks = MakeHooks(log) }) == JobStatus::Ok);
            JobGraph graph;
            REQUIRE(graph.Initialize({ .MaxJobs = 3, .MaxDependencies = 1 }) == JobStatus::Ok);
            std::atomic<uint32> calls{0};
            JobHandle failed{};
            JobHandle blocked{};
            JobHandle independent{};
            REQUIRE(graph.Add(failure, nullptr, failed) == JobStatus::Ok);
            REQUIRE(graph.Add(CountCall, &calls, blocked) == JobStatus::Ok);
            REQUIRE(graph.Add(CountCall, &calls, independent) == JobStatus::Ok);
            REQUIRE(graph.DependsOn(blocked, failed) == JobStatus::Ok);
            REQUIRE(graph.Seal() == JobStatus::Ok);
            REQUIRE(system.Submit(graph) == JobStatus::Ok);
            JobOutcome outcome{};
            REQUIRE(system.Wait(outcome) == JobStatus::Ok);
            REQUIRE(outcome == JobOutcome::Failed);
            REQUIRE(log.CountKind(JobTraceKind::Signalled) == 0);
            REQUIRE(log.Find(JobTraceKind::Started, blocked.Index) == UNSET);
            REQUIRE(log.CountKind(JobTraceKind::Started) == 2);
            REQUIRE(log.Events[log.Find(JobTraceKind::Finished, failed.Index)].Event.Outcome == JobOutcome::Failed);
            REQUIRE(log.Events[log.GetCount() - 1].Event.Outcome == JobOutcome::Failed);
            REQUIRE(calls == 1);
        }
    }
}

TEST_CASE("A declining trace filter leaves the submission untraced", "[threading][trace]")
{
    for (const uint32 workers : {0u, 2u})
    {
        TraceLog log;
        log.ShouldTrace = false;
        JobSystem system;
        REQUIRE(system.Initialize({ .WorkerCount = workers, .Hooks = MakeHooks(log) }) == JobStatus::Ok);
        JobGraph graph;
        REQUIRE(graph.Initialize({ .MaxJobs = 2, .MaxDependencies = 1 }) == JobStatus::Ok);
        std::atomic<uint32> calls{0};
        JobHandle first{};
        JobHandle second{};
        REQUIRE(graph.Add(CountCall, &calls, first) == JobStatus::Ok);
        REQUIRE(graph.Add(CountCall, &calls, second) == JobStatus::Ok);
        REQUIRE(graph.DependsOn(second, first) == JobStatus::Ok);
        REQUIRE(graph.Seal() == JobStatus::Ok);
        for (const bool shouldTrace : {false, true, false})
        {
            log.ShouldTrace = shouldTrace;
            log.Count = 0;
            REQUIRE(system.Submit(graph) == JobStatus::Ok);
            JobOutcome outcome{};
            REQUIRE(system.Wait(outcome) == JobStatus::Ok);
            REQUIRE(outcome == JobOutcome::Succeeded);
            // A submission is traced completely or not at all.
            REQUIRE(log.Count.load() == (shouldTrace == true ? 8u : 0u));
        }
        REQUIRE(log.FilterCalls == 3);
        REQUIRE(calls == 6);
    }
}

TEST_CASE("Worker hooks run once per worker during Initialize and Shutdown", "[threading][trace]")
{
    for (const uint32 workers : {0u, 1u, 4u})
    {
        WorkerHookLog log;
        JobSystem system;
        log.SystemOrNull = &system;
        log.CallerOrdinal = GetThreadOrdinal();
        const JobSystemHooks hooks
        {
            .WorkerStartOrNull = StartWorker,
            .WorkerStopOrNull = StopWorker,
            .ContextOrNull = &log,
        };
        REQUIRE(system.Initialize({ .WorkerCount = workers, .Hooks = hooks }) == JobStatus::Ok);
        // Setup completes before Initialize returns, on the workers themselves.
        REQUIRE(log.Starts == workers);
        REQUIRE(log.StartedMask == (uint32{1} << workers) - 1);
        REQUIRE(log.CallerStarts == 0);
        REQUIRE(log.InsideJobResults == workers);
        REQUIRE(log.Stops == 0);
        REQUIRE(system.Shutdown() == JobStatus::Ok);
        REQUIRE(log.Stops == workers);
        REQUIRE(system.Initialize({ .WorkerCount = workers, .Hooks = hooks }) == JobStatus::Ok);
        REQUIRE(log.Starts == 2 * workers);
    }
}
