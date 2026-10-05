#include <ludus/foundation/threading/jobs.hpp>

#include "internal/native.hpp"

#include <atomic>
#include <new>

// Thanks to Julien Hamaide, "Multithread Job and Dependency System," Game
// Programming Gems 7, ch. 1.9, pp. 87-96, for dependency counters and versioned
// identity; Brad Werth, "Holistic Task Parallelism for Common Game Architecture
// Patterns," Game Engine Gems 1, ch. 22, pp. 381-390, for continuations/helping;
// and Jean-François Dubé, "Efficient and Scalable Multi-Core Programming," Game
// Programming Gems 8, ch. 4.3, pp. 373-384, for sleeping workers and allocation
// discipline. This implementation uses a bounded gate-protected queue and C++23
// publication, with distinct failure/cancellation outcomes, rather than their
// historical volatile/spinning examples. Independently written code; full source
// review and departures: docs/architecture/threading.md.
namespace ludus::foundation::threading
{
namespace detail
{
constexpr uint32 INVALID_INDEX = static_cast<uint32>(-1);
constexpr uint32 MAX_WORKERS = 64;
constexpr uint32 MAX_RECORDS = 1u << 20;
thread_local bool gInsideJob = false;

struct Node
{
    JobFunction Function = nullptr;
    void* Context = nullptr;
    uint32 FirstEdge = INVALID_INDEX;
    uint32 Incoming = 0;
    uint32 Remaining = 0;
    bool PrerequisiteFailed = false;
    std::atomic<JobOutcome> Outcome{JobOutcome::Pending};
};
struct Edge
{
    uint32 To = 0;
    uint32 Next = INVALID_INDEX;
};
struct GraphState
{
    Node* Nodes = nullptr;
    Edge* Edges = nullptr;
    uint32* Ready = nullptr;
    JobGraphConfig Config{};
    uint64 Id = 0;
    uint64 Generation = 1;
    uint32 JobCount = 0;
    uint32 EdgeCount = 0;
    uint32 Head = 0;
    uint32 ReadyCount = 0;
    uint32 Unfinished = 0;
    bool Sealed = false;
    bool CancelRequested = false;
    bool AnyFailed = false;
    bool AnyCancelled = false;
    std::atomic<JobSystem*> Owner{nullptr};

    ~GraphState() noexcept
    {
        delete[] Nodes;
        delete[] Edges;
        delete[] Ready;
    }
    [[nodiscard]] bool Valid(JobHandle handle) const noexcept
    {
        return handle.GraphId == Id && handle.Generation == Generation && handle.Index < JobCount;
    }
    void Push(uint32 index) noexcept
    {
        LUDUS_REQUIRE(ReadyCount < Config.MaxJobs);
        Ready[(Head + ReadyCount) % Config.MaxJobs] = index;
        ++ReadyCount;
    }
    [[nodiscard]] uint32 Pop() noexcept
    {
        const uint32 index = Ready[Head];
        Head = (Head + 1) % Config.MaxJobs;
        --ReadyCount;
        return index;
    }
};
struct SystemState
{
    NativeGate Gate{};
    NativeThread Threads[MAX_WORKERS]{};
    uint32 WorkerCount = 0;
    GraphState* Active = nullptr;
    bool Stop = false;
};

namespace
{
std::atomic<uint64> gNextGraphId{1};
[[nodiscard]] uint64 NewGraphId() noexcept
{
    uint64 id = gNextGraphId.load(std::memory_order_relaxed);
    while (id != static_cast<uint64>(-1))
    {
        if (gNextGraphId.compare_exchange_weak(id, id + 1, std::memory_order_relaxed))
        {
            return id;
        }
    }
    return 0;
}

// Called with the system gate held. Every node enters Ready exactly once.
void Complete(SystemState& system, uint32 index, JobOutcome outcome) noexcept
{
    auto& graph = *system.Active;
    auto& node = graph.Nodes[index];
    node.Outcome.store(outcome, std::memory_order_release);
    graph.AnyFailed |= outcome == JobOutcome::Failed;
    graph.AnyCancelled |= outcome == JobOutcome::Cancelled;
    const bool readyWasEmpty = graph.ReadyCount == 0;
    for (uint32 edge = node.FirstEdge; edge != INVALID_INDEX; edge = graph.Edges[edge].Next)
    {
        auto& dependent = graph.Nodes[graph.Edges[edge].To];
        dependent.PrerequisiteFailed |= outcome != JobOutcome::Succeeded;
        LUDUS_REQUIRE(dependent.Remaining > 0);
        if (--dependent.Remaining == 0)
        {
            graph.Push(graph.Edges[edge].To);
        }
    }
    --graph.Unfinished;
    // One predicate/gate for ready work, completion and stop. Publish while
    // holding the gate so sleepers cannot miss a transition before sleeping.
    if ((readyWasEmpty && graph.ReadyCount > 0) || graph.Unfinished == 0)
    {
        system.Gate.WakeAll();
    }
}

// The caller holds the gate on entry and return. User code runs outside it.
bool ExecuteOne(SystemState& system) noexcept
{
    auto* graph = system.Active;
    if (graph == nullptr || graph->ReadyCount == 0)
    {
        return false;
    }
    const uint32 index = graph->Pop();
    auto& node = graph->Nodes[index];
    if (graph->CancelRequested || node.PrerequisiteFailed)
    {
        Complete(system, index, graph->CancelRequested ? JobOutcome::Cancelled : JobOutcome::Blocked);
        return true;
    }
    node.Outcome.store(JobOutcome::Running, std::memory_order_release);
    system.Gate.Unlock();
    gInsideJob = true;
    JobOutcome outcome = node.Function(node.Context);
    gInsideJob = false;
    if (outcome != JobOutcome::Succeeded && outcome != JobOutcome::Failed && outcome != JobOutcome::Cancelled)
    {
        outcome = JobOutcome::Failed;
    }
    system.Gate.Lock();
    Complete(system, index, outcome);
    return true;
}

void WorkerEntry(void* context) noexcept
{
    auto& system = *static_cast<SystemState*>(context);
    GateLock lock(system.Gate);
    while (!system.Stop)
    {
        if (!ExecuteOne(system))
        {
            system.Gate.Wait();
        }
    }
}
} // namespace
} // namespace detail

using detail::GateLock;
using detail::GraphState;
using detail::INVALID_INDEX;

JobGraph::~JobGraph() noexcept
{
    if (mState != nullptr)
    {
        if (auto* owner = mState->Owner.load(std::memory_order_acquire))
        {
            // Destruction on a callback stack cannot safely drain that callback.
            LUDUS_REQUIRE(!detail::gInsideJob);
            JobOutcome outcome{};
            (void)owner->Wait(outcome);
        }
        delete mState;
    }
}

JobStatus JobGraph::Initialize(JobGraphConfig config) noexcept
{
    if (mState != nullptr)
    {
        return JobStatus::AlreadyInitialized;
    }
    if (config.MaxJobs == 0 || config.MaxJobs > detail::MAX_RECORDS || config.MaxDependencies > detail::MAX_RECORDS)
    {
        return JobStatus::InvalidArgument;
    }
    auto* state = new (std::nothrow) GraphState{};
    if (state == nullptr)
    {
        return JobStatus::OutOfMemory;
    }
    state->Config = config;
    state->Nodes = new (std::nothrow) detail::Node[config.MaxJobs];
    state->Ready = new (std::nothrow) uint32[config.MaxJobs];
    if (config.MaxDependencies > 0)
    {
        state->Edges = new (std::nothrow) detail::Edge[config.MaxDependencies];
    }
    if (state->Nodes == nullptr || state->Ready == nullptr || (config.MaxDependencies > 0 && state->Edges == nullptr))
    {
        delete state;
        return JobStatus::OutOfMemory;
    }
    state->Id = detail::NewGraphId();
    if (state->Id == 0)
    {
        delete state;
        return JobStatus::IdentityExhausted;
    }
    mState = state;
    return JobStatus::Ok;
}

JobStatus JobGraph::Add(JobFunction function, void* context, JobHandle& handle) noexcept
{
    handle = {};
    if (mState == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    auto& graph = *mState;
    if (graph.Owner.load(std::memory_order_acquire) != nullptr || graph.Sealed)
    {
        return JobStatus::Busy;
    }
    if (function == nullptr)
    {
        return JobStatus::InvalidArgument;
    }
    if (graph.JobCount == graph.Config.MaxJobs)
    {
        return JobStatus::CapacityExceeded;
    }
    const uint32 index = graph.JobCount++;
    auto& node = graph.Nodes[index];
    node.Function = function;
    node.Context = context;
    handle = {graph.Id, graph.Generation, index};
    return JobStatus::Ok;
}

JobStatus JobGraph::DependsOn(JobHandle dependent, JobHandle prerequisite) noexcept
{
    if (mState == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    auto& graph = *mState;
    if (graph.Owner.load(std::memory_order_acquire) != nullptr || graph.Sealed)
    {
        return JobStatus::Busy;
    }
    if (!graph.Valid(dependent) || !graph.Valid(prerequisite))
    {
        return JobStatus::InvalidHandle;
    }
    if (dependent.Index == prerequisite.Index)
    {
        return JobStatus::CycleDetected;
    }
    auto& from = graph.Nodes[prerequisite.Index];
    for (uint32 edge = from.FirstEdge; edge != INVALID_INDEX; edge = graph.Edges[edge].Next)
    {
        if (graph.Edges[edge].To == dependent.Index)
        {
            return JobStatus::DuplicateDependency;
        }
    }
    if (graph.EdgeCount == graph.Config.MaxDependencies)
    {
        return JobStatus::CapacityExceeded;
    }
    graph.Edges[graph.EdgeCount] = {dependent.Index, from.FirstEdge};
    from.FirstEdge = graph.EdgeCount++;
    ++graph.Nodes[dependent.Index].Incoming;
    return JobStatus::Ok;
}

JobStatus JobGraph::Seal() noexcept
{
    if (mState == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    auto& graph = *mState;
    if (graph.Owner.load(std::memory_order_acquire) != nullptr)
    {
        return JobStatus::Busy;
    }
    // Kahn validation reuses the runtime ready ring and dependency counters.
    graph.Head = 0;
    graph.ReadyCount = 0;
    for (uint32 index = 0; index < graph.JobCount; ++index)
    {
        graph.Nodes[index].Remaining = graph.Nodes[index].Incoming;
        if (graph.Nodes[index].Incoming == 0)
        {
            graph.Push(index);
        }
    }
    uint32 visited = 0;
    while (graph.ReadyCount > 0)
    {
        const uint32 index = graph.Pop();
        ++visited;
        for (uint32 edge = graph.Nodes[index].FirstEdge; edge != INVALID_INDEX; edge = graph.Edges[edge].Next)
        {
            if (--graph.Nodes[graph.Edges[edge].To].Remaining == 0)
            {
                graph.Push(graph.Edges[edge].To);
            }
        }
    }
    graph.Sealed = visited == graph.JobCount;
    return graph.Sealed ? JobStatus::Ok : JobStatus::CycleDetected;
}

JobStatus JobGraph::Reset() noexcept
{
    if (mState == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    auto& graph = *mState;
    if (graph.Owner.load(std::memory_order_acquire) != nullptr)
    {
        return JobStatus::Busy;
    }
    if (graph.Generation == static_cast<uint64>(-1))
    {
        return JobStatus::IdentityExhausted;
    }
    for (uint32 index = 0; index < graph.JobCount; ++index)
    {
        auto& node = graph.Nodes[index];
        node.Function = nullptr;
        node.Context = nullptr;
        node.FirstEdge = INVALID_INDEX;
        node.Incoming = 0;
        node.Outcome.store(JobOutcome::Pending, std::memory_order_relaxed);
    }
    ++graph.Generation;
    graph.JobCount = 0;
    graph.EdgeCount = 0;
    graph.Sealed = false;
    return JobStatus::Ok;
}

JobStatus JobGraph::GetOutcome(JobHandle handle, JobOutcome& outcome) const noexcept
{
    outcome = JobOutcome::Pending;
    if (mState == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    if (!mState->Valid(handle))
    {
        return JobStatus::InvalidHandle;
    }
    outcome = mState->Nodes[handle.Index].Outcome.load(std::memory_order_acquire);
    return JobStatus::Ok;
}

JobSystem::~JobSystem() noexcept
{
    LUDUS_REQUIRE(!detail::gInsideJob);
    (void)Shutdown();
}

JobStatus JobSystem::Initialize(JobSystemConfig config) noexcept
{
    if (detail::gInsideJob)
    {
        return JobStatus::InsideJob;
    }
    if (mState != nullptr)
    {
        return JobStatus::AlreadyInitialized;
    }
    if (config.WorkerCount > detail::MAX_WORKERS)
    {
        return JobStatus::InvalidArgument;
    }
#if defined(LUDUS_PLATFORM_WEB)
    if (config.WorkerCount != 0)
    {
        return JobStatus::Unsupported;
    }
#endif
    auto* state = new (std::nothrow) detail::SystemState{};
    if (state == nullptr)
    {
        return JobStatus::OutOfMemory;
    }
    if (!state->Gate.Initialize())
    {
        delete state;
        return JobStatus::ThreadCreationFailed;
    }
    mState = state;
    for (uint32 index = 0; index < config.WorkerCount; ++index)
    {
        if (!state->Threads[index].Start(detail::WorkerEntry, state))
        {
            (void)Shutdown();
            return JobStatus::ThreadCreationFailed;
        }
        ++state->WorkerCount;
    }
    return JobStatus::Ok;
}

JobStatus JobSystem::Submit(JobGraph& graph) noexcept
{
    if (detail::gInsideJob)
    {
        return JobStatus::InsideJob;
    }
    if (mState == nullptr || graph.mState == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    GateLock lock(mState->Gate);
    auto& state = *graph.mState;
    if (mState->Active != nullptr)
    {
        return JobStatus::Busy;
    }
    if (!state.Sealed)
    {
        return JobStatus::NotSealed;
    }
    JobSystem* expected = nullptr;
    if (!state.Owner.compare_exchange_strong(expected, this, std::memory_order_acq_rel))
    {
        return JobStatus::Busy;
    }
    state.Head = 0;
    state.ReadyCount = 0;
    state.Unfinished = state.JobCount;
    state.CancelRequested = false;
    state.AnyFailed = false;
    state.AnyCancelled = false;
    for (uint32 index = 0; index < state.JobCount; ++index)
    {
        auto& node = state.Nodes[index];
        node.Remaining = node.Incoming;
        node.PrerequisiteFailed = false;
        node.Outcome.store(JobOutcome::Pending, std::memory_order_relaxed);
        if (node.Remaining == 0)
        {
            state.Push(index);
        }
    }
    mState->Active = &state;
    mState->Gate.WakeAll();
    if (mState->WorkerCount == 0)
    {
        while (detail::ExecuteOne(*mState))
        {
        }
    }
    return JobStatus::Ok;
}

JobStatus JobSystem::Wait(JobOutcome& outcome, bool help) noexcept
{
    outcome = JobOutcome::Pending;
    if (detail::gInsideJob)
    {
        return JobStatus::InsideJob;
    }
    if (mState == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    GateLock lock(mState->Gate);
    auto* graph = mState->Active;
    if (graph == nullptr)
    {
        return JobStatus::InvalidArgument;
    }
    while (graph->Unfinished > 0)
    {
        if (!help || !detail::ExecuteOne(*mState))
        {
            mState->Gate.Wait();
        }
    }
    outcome = graph->AnyFailed      ? JobOutcome::Failed
              : graph->AnyCancelled ? JobOutcome::Cancelled
                                    : JobOutcome::Succeeded;
    mState->Active = nullptr;
    graph->Owner.store(nullptr, std::memory_order_release);
    return JobStatus::Ok;
}

JobStatus JobSystem::Cancel() noexcept
{
    if (mState == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    GateLock lock(mState->Gate);
    if (mState->Active == nullptr)
    {
        return JobStatus::InvalidArgument;
    }
    mState->Active->CancelRequested = true;
    mState->Gate.WakeAll();
    return JobStatus::Ok;
}

JobStatus JobSystem::Snapshot(JobSnapshot& snapshot) const noexcept
{
    snapshot = {};
    if (mState == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    GateLock lock(mState->Gate);
    if (mState->Active == nullptr)
    {
        return JobStatus::Ok;
    }
    for (uint32 index = 0; index < mState->Active->JobCount; ++index)
    {
        switch (mState->Active->Nodes[index].Outcome.load(std::memory_order_relaxed))
        {
            case JobOutcome::Pending:
                ++snapshot.Pending;
                break;
            case JobOutcome::Running:
                ++snapshot.Running;
                break;
            case JobOutcome::Succeeded:
                ++snapshot.Succeeded;
                break;
            case JobOutcome::Failed:
                ++snapshot.Failed;
                break;
            case JobOutcome::Cancelled:
                ++snapshot.Cancelled;
                break;
            case JobOutcome::Blocked:
                ++snapshot.Blocked;
                break;
        }
    }
    return JobStatus::Ok;
}

JobStatus JobSystem::Shutdown() noexcept
{
    if (detail::gInsideJob)
    {
        return JobStatus::InsideJob;
    }
    if (mState == nullptr)
    {
        return JobStatus::Ok;
    }
    JobOutcome outcome{};
    (void)Wait(outcome);
    {
        GateLock lock(mState->Gate);
        mState->Stop = true;
        mState->Gate.WakeAll();
    }
    for (uint32 index = 0; index < mState->WorkerCount; ++index)
    {
        mState->Threads[index].Join();
    }
    mState->Gate.Destroy();
    delete mState;
    mState = nullptr;
    return JobStatus::Ok;
}
} // namespace ludus::foundation::threading
