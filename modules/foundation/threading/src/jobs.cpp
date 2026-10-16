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
thread_local bool isInsideJob = false;

struct Node
{
    JobFunction Function = nullptr;
    void* ContextOrNull = nullptr;
    uint32 FirstEdge = INVALID_INDEX;
    uint32 Incoming = 0;
    uint32 Remaining = 0;
    bool HasFailedPrerequisite = false;
    std::atomic<JobOutcome> Outcome{JobOutcome::Pending};
};
struct Edge
{
    uint32 To = 0;
    uint32 Next = INVALID_INDEX;
};
struct GraphState
{
    // Initialize allocates every array, including a zero-length edge array, so
    // these pointers are nonnull for the state's lifetime.
    GraphState(Node* nodes, Edge* edges, uint32* ready, JobGraphConfig config, uint64 id) noexcept
        : Nodes(nodes), Edges(edges), Ready(ready), Config(config), Id(id)
    {
    }
    ~GraphState() noexcept
    {
        delete[] Nodes;
        delete[] Edges;
        delete[] Ready;
    }
    GraphState(const GraphState&) = delete;
    GraphState& operator=(const GraphState&) = delete;

    Node* Nodes;
    Edge* Edges;
    uint32* Ready;
    JobGraphConfig Config;
    uint64 Id;
    uint64 Generation = 1;
    uint32 JobCount = 0;
    uint32 EdgeCount = 0;
    uint32 Head = 0;
    uint32 ReadyCount = 0;
    uint32 Unfinished = 0;
    bool IsSealed = false;
    bool IsCancelRequested = false;
    bool HasFailure = false;
    bool HasCancellation = false;
    std::atomic<JobSystem*> OwnerOrNull{nullptr};

    [[nodiscard]] bool IsValid(JobHandle handle) const noexcept
    {
        return handle.GraphId == Id && handle.Generation == Generation && handle.Index < JobCount;
    }
    void PushReady(uint32 index) noexcept
    {
        LUDUS_REQUIRE(ReadyCount < Config.MaxJobs);
        Ready[(Head + ReadyCount) % Config.MaxJobs] = index;
        ++ReadyCount;
    }
    [[nodiscard]] uint32 PopReady() noexcept
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
    // FoundationThreading depends only on FoundationBase, so the fixed worker
    // table stays a raw array rather than a Containers StaticArray.
    NativeThread Threads[MAX_WORKERS]{};
    uint32 WorkerCount = 0;
    GraphState* ActiveOrNull = nullptr;
    bool ShouldStop = false;
};

namespace
{
std::atomic<uint64> gNextGraphId{1};
// Returns zero once the identity space is exhausted; it never wraps.
[[nodiscard]] uint64 AllocateGraphId() noexcept
{
    uint64 id = gNextGraphId.load(std::memory_order_relaxed);
    while (id != static_cast<uint64>(-1))
    {
        if (gNextGraphId.compare_exchange_weak(id, id + 1, std::memory_order_relaxed) == true)
        {
            return id;
        }
    }
    return 0;
}

// Called with the system gate held. Every node enters Ready exactly once.
void CompleteJob(SystemState& inoutSystem, uint32 index, JobOutcome outcome) noexcept
{
    GraphState& graph = *inoutSystem.ActiveOrNull;
    Node& node = graph.Nodes[index];
    node.Outcome.store(outcome, std::memory_order_release);
    graph.HasFailure |= outcome == JobOutcome::Failed;
    graph.HasCancellation |= outcome == JobOutcome::Cancelled;
    const bool wasReadyEmpty = graph.ReadyCount == 0;
    for (uint32 edge = node.FirstEdge; edge != INVALID_INDEX; edge = graph.Edges[edge].Next)
    {
        Node& dependent = graph.Nodes[graph.Edges[edge].To];
        dependent.HasFailedPrerequisite |= outcome != JobOutcome::Succeeded;
        LUDUS_REQUIRE(dependent.Remaining > 0);
        if (--dependent.Remaining == 0)
        {
            graph.PushReady(graph.Edges[edge].To);
        }
    }
    --graph.Unfinished;
    // One predicate/gate for ready work, completion and stop. Publish while
    // holding the gate so sleepers cannot miss a transition before sleeping.
    if ((wasReadyEmpty == true && graph.ReadyCount > 0) || graph.Unfinished == 0)
    {
        inoutSystem.Gate.WakeAll();
    }
}

// Resolves one ready job without calling it: an explicit Cancel takes
// precedence over a failed prerequisite.
[[nodiscard]] JobOutcome GetSkippedOutcome(const GraphState& graph, const Node& node) noexcept
{
    if (graph.IsCancelRequested == true)
    {
        return JobOutcome::Cancelled;
    }
    return node.HasFailedPrerequisite == true ? JobOutcome::Blocked : JobOutcome::Pending;
}

// The caller holds the gate on entry and return. User code runs outside it.
bool ExecuteOneJob(SystemState& inoutSystem) noexcept
{
    GraphState* graphOrNull = inoutSystem.ActiveOrNull;
    if (graphOrNull == nullptr || graphOrNull->ReadyCount == 0)
    {
        return false;
    }
    const uint32 index = graphOrNull->PopReady();
    Node& node = graphOrNull->Nodes[index];
    const JobOutcome skippedOutcome = GetSkippedOutcome(*graphOrNull, node);
    if (skippedOutcome != JobOutcome::Pending)
    {
        CompleteJob(inoutSystem, index, skippedOutcome);
        return true;
    }
    node.Outcome.store(JobOutcome::Running, std::memory_order_release);
    inoutSystem.Gate.Unlock();
    isInsideJob = true;
    JobOutcome outcome = node.Function(node.ContextOrNull);
    isInsideJob = false;
    if (outcome != JobOutcome::Succeeded && outcome != JobOutcome::Failed && outcome != JobOutcome::Cancelled)
    {
        outcome = JobOutcome::Failed;
    }
    inoutSystem.Gate.Lock();
    CompleteJob(inoutSystem, index, outcome);
    return true;
}

void RunWorker(void* context) noexcept
{
    SystemState& system = *static_cast<SystemState*>(context);
    GateLock lock(system.Gate);
    while (system.ShouldStop == false)
    {
        if (ExecuteOneJob(system) == false)
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
using detail::Node;

JobGraph::~JobGraph() noexcept
{
    if (mStateOrNull != nullptr)
    {
        JobSystem* ownerOrNull = mStateOrNull->OwnerOrNull.load(std::memory_order_acquire);
        if (ownerOrNull != nullptr)
        {
            // Destruction on a callback stack cannot safely drain that callback.
            LUDUS_REQUIRE(detail::isInsideJob == false);
            JobOutcome outcome{};
            (void)ownerOrNull->Wait(outcome);
        }
        delete mStateOrNull;
    }
}

JobStatus JobGraph::Initialize(JobGraphConfig config) noexcept
{
    if (mStateOrNull != nullptr)
    {
        return JobStatus::AlreadyInitialized;
    }
    if (config.MaxJobs == 0 || config.MaxJobs > detail::MAX_RECORDS || config.MaxDependencies > detail::MAX_RECORDS)
    {
        return JobStatus::InvalidArgument;
    }
    Node* nodesOrNull = new (std::nothrow) Node[config.MaxJobs];
    // A zero-length array new still returns a unique nonnull pointer.
    detail::Edge* edgesOrNull = new (std::nothrow) detail::Edge[config.MaxDependencies];
    uint32* readyOrNull = new (std::nothrow) uint32[config.MaxJobs];
    if (nodesOrNull == nullptr || edgesOrNull == nullptr || readyOrNull == nullptr)
    {
        delete[] nodesOrNull;
        delete[] edgesOrNull;
        delete[] readyOrNull;
        return JobStatus::OutOfMemory;
    }
    GraphState* stateOrNull = new (std::nothrow) GraphState(nodesOrNull, edgesOrNull, readyOrNull, config, 0);
    if (stateOrNull == nullptr)
    {
        delete[] nodesOrNull;
        delete[] edgesOrNull;
        delete[] readyOrNull;
        return JobStatus::OutOfMemory;
    }
    stateOrNull->Id = detail::AllocateGraphId();
    if (stateOrNull->Id == 0)
    {
        delete stateOrNull;
        return JobStatus::IdentityExhausted;
    }
    mStateOrNull = stateOrNull;
    return JobStatus::Ok;
}

JobStatus JobGraph::Add(JobFunction function, void* contextOrNull, JobHandle& outHandle) noexcept
{
    outHandle = {};
    if (mStateOrNull == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    GraphState& graph = *mStateOrNull;
    if (graph.OwnerOrNull.load(std::memory_order_acquire) != nullptr || graph.IsSealed == true)
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
    Node& node = graph.Nodes[index];
    node.Function = function;
    node.ContextOrNull = contextOrNull;
    outHandle = { .GraphId = graph.Id, .Generation = graph.Generation, .Index = index };
    return JobStatus::Ok;
}

JobStatus JobGraph::DependsOn(JobHandle dependent, JobHandle prerequisite) noexcept
{
    if (mStateOrNull == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    GraphState& graph = *mStateOrNull;
    if (graph.OwnerOrNull.load(std::memory_order_acquire) != nullptr || graph.IsSealed == true)
    {
        return JobStatus::Busy;
    }
    if (graph.IsValid(dependent) == false || graph.IsValid(prerequisite) == false)
    {
        return JobStatus::InvalidHandle;
    }
    if (dependent.Index == prerequisite.Index)
    {
        return JobStatus::CycleDetected;
    }
    Node& from = graph.Nodes[prerequisite.Index];
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
    graph.Edges[graph.EdgeCount] = { .To = dependent.Index, .Next = from.FirstEdge };
    from.FirstEdge = graph.EdgeCount++;
    ++graph.Nodes[dependent.Index].Incoming;
    return JobStatus::Ok;
}

JobStatus JobGraph::Seal() noexcept
{
    if (mStateOrNull == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    GraphState& graph = *mStateOrNull;
    if (graph.OwnerOrNull.load(std::memory_order_acquire) != nullptr)
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
            graph.PushReady(index);
        }
    }
    uint32 visited = 0;
    while (graph.ReadyCount > 0)
    {
        const uint32 index = graph.PopReady();
        ++visited;
        for (uint32 edge = graph.Nodes[index].FirstEdge; edge != INVALID_INDEX; edge = graph.Edges[edge].Next)
        {
            if (--graph.Nodes[graph.Edges[edge].To].Remaining == 0)
            {
                graph.PushReady(graph.Edges[edge].To);
            }
        }
    }
    graph.IsSealed = visited == graph.JobCount;
    return graph.IsSealed == true ? JobStatus::Ok : JobStatus::CycleDetected;
}

JobStatus JobGraph::Reset() noexcept
{
    if (mStateOrNull == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    GraphState& graph = *mStateOrNull;
    if (graph.OwnerOrNull.load(std::memory_order_acquire) != nullptr)
    {
        return JobStatus::Busy;
    }
    if (graph.Generation == static_cast<uint64>(-1))
    {
        return JobStatus::IdentityExhausted;
    }
    for (uint32 index = 0; index < graph.JobCount; ++index)
    {
        Node& node = graph.Nodes[index];
        node.Function = nullptr;
        node.ContextOrNull = nullptr;
        node.FirstEdge = INVALID_INDEX;
        node.Incoming = 0;
        node.Outcome.store(JobOutcome::Pending, std::memory_order_relaxed);
    }
    ++graph.Generation;
    graph.JobCount = 0;
    graph.EdgeCount = 0;
    graph.IsSealed = false;
    return JobStatus::Ok;
}

JobStatus JobGraph::GetOutcome(JobHandle handle, JobOutcome& outOutcome) const noexcept
{
    outOutcome = JobOutcome::Pending;
    if (mStateOrNull == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    if (mStateOrNull->IsValid(handle) == false)
    {
        return JobStatus::InvalidHandle;
    }
    outOutcome = mStateOrNull->Nodes[handle.Index].Outcome.load(std::memory_order_acquire);
    return JobStatus::Ok;
}

JobSystem::~JobSystem() noexcept
{
    LUDUS_REQUIRE(detail::isInsideJob == false);
    (void)Shutdown();
}

JobStatus JobSystem::Initialize(JobSystemConfig config) noexcept
{
    if (detail::isInsideJob == true)
    {
        return JobStatus::InsideJob;
    }
    if (mStateOrNull != nullptr)
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
    detail::SystemState* stateOrNull = new (std::nothrow) detail::SystemState{};
    if (stateOrNull == nullptr)
    {
        return JobStatus::OutOfMemory;
    }
    if (stateOrNull->Gate.Initialize() == false)
    {
        delete stateOrNull;
        return JobStatus::ThreadCreationFailed;
    }
    mStateOrNull = stateOrNull;
    for (uint32 index = 0; index < config.WorkerCount; ++index)
    {
        if (stateOrNull->Threads[index].Start(detail::RunWorker, stateOrNull) == false)
        {
            (void)Shutdown();
            return JobStatus::ThreadCreationFailed;
        }
        ++stateOrNull->WorkerCount;
    }
    return JobStatus::Ok;
}

JobStatus JobSystem::Submit(JobGraph& inoutGraph) noexcept
{
    if (detail::isInsideJob == true)
    {
        return JobStatus::InsideJob;
    }
    if (mStateOrNull == nullptr || inoutGraph.mStateOrNull == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    GateLock lock(mStateOrNull->Gate);
    GraphState& graph = *inoutGraph.mStateOrNull;
    if (mStateOrNull->ActiveOrNull != nullptr)
    {
        return JobStatus::Busy;
    }
    if (graph.IsSealed == false)
    {
        return JobStatus::NotSealed;
    }
    JobSystem* expectedOrNull = nullptr;
    if (graph.OwnerOrNull.compare_exchange_strong(expectedOrNull, this, std::memory_order_acq_rel) == false)
    {
        return JobStatus::Busy;
    }
    graph.Head = 0;
    graph.ReadyCount = 0;
    graph.Unfinished = graph.JobCount;
    graph.IsCancelRequested = false;
    graph.HasFailure = false;
    graph.HasCancellation = false;
    for (uint32 index = 0; index < graph.JobCount; ++index)
    {
        Node& node = graph.Nodes[index];
        node.Remaining = node.Incoming;
        node.HasFailedPrerequisite = false;
        node.Outcome.store(JobOutcome::Pending, std::memory_order_relaxed);
        if (node.Remaining == 0)
        {
            graph.PushReady(index);
        }
    }
    mStateOrNull->ActiveOrNull = &graph;
    mStateOrNull->Gate.WakeAll();
    if (mStateOrNull->WorkerCount == 0)
    {
        while (detail::ExecuteOneJob(*mStateOrNull) == true)
        {
        }
    }
    return JobStatus::Ok;
}

JobStatus JobSystem::Wait(JobOutcome& outOutcome, bool shouldHelp) noexcept
{
    outOutcome = JobOutcome::Pending;
    if (detail::isInsideJob == true)
    {
        return JobStatus::InsideJob;
    }
    if (mStateOrNull == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    GateLock lock(mStateOrNull->Gate);
    GraphState* graphOrNull = mStateOrNull->ActiveOrNull;
    if (graphOrNull == nullptr)
    {
        return JobStatus::InvalidArgument;
    }
    while (graphOrNull->Unfinished > 0)
    {
        if (shouldHelp == false || detail::ExecuteOneJob(*mStateOrNull) == false)
        {
            mStateOrNull->Gate.Wait();
        }
    }
    if (graphOrNull->HasFailure == true)
    {
        outOutcome = JobOutcome::Failed;
    }
    else
    {
        outOutcome = graphOrNull->HasCancellation == true ? JobOutcome::Cancelled : JobOutcome::Succeeded;
    }
    mStateOrNull->ActiveOrNull = nullptr;
    graphOrNull->OwnerOrNull.store(nullptr, std::memory_order_release);
    return JobStatus::Ok;
}

JobStatus JobSystem::Cancel() noexcept
{
    if (mStateOrNull == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    GateLock lock(mStateOrNull->Gate);
    if (mStateOrNull->ActiveOrNull == nullptr)
    {
        return JobStatus::InvalidArgument;
    }
    mStateOrNull->ActiveOrNull->IsCancelRequested = true;
    mStateOrNull->Gate.WakeAll();
    return JobStatus::Ok;
}

JobStatus JobSystem::Snapshot(JobSnapshot& outSnapshot) const noexcept
{
    outSnapshot = {};
    if (mStateOrNull == nullptr)
    {
        return JobStatus::NotInitialized;
    }
    GateLock lock(mStateOrNull->Gate);
    const GraphState* graphOrNull = mStateOrNull->ActiveOrNull;
    if (graphOrNull == nullptr)
    {
        return JobStatus::Ok;
    }
    for (uint32 index = 0; index < graphOrNull->JobCount; ++index)
    {
        switch (graphOrNull->Nodes[index].Outcome.load(std::memory_order_relaxed))
        {
            case JobOutcome::Pending:
                ++outSnapshot.Pending;
                break;
            case JobOutcome::Running:
                ++outSnapshot.Running;
                break;
            case JobOutcome::Succeeded:
                ++outSnapshot.Succeeded;
                break;
            case JobOutcome::Failed:
                ++outSnapshot.Failed;
                break;
            case JobOutcome::Cancelled:
                ++outSnapshot.Cancelled;
                break;
            case JobOutcome::Blocked:
                ++outSnapshot.Blocked;
                break;
        }
    }
    return JobStatus::Ok;
}

JobStatus JobSystem::Shutdown() noexcept
{
    if (detail::isInsideJob == true)
    {
        return JobStatus::InsideJob;
    }
    if (mStateOrNull == nullptr)
    {
        return JobStatus::Ok;
    }
    JobOutcome outcome{};
    (void)Wait(outcome);
    {
        GateLock lock(mStateOrNull->Gate);
        mStateOrNull->ShouldStop = true;
        mStateOrNull->Gate.WakeAll();
    }
    for (uint32 index = 0; index < mStateOrNull->WorkerCount; ++index)
    {
        mStateOrNull->Threads[index].Join();
    }
    mStateOrNull->Gate.Destroy();
    delete mStateOrNull;
    mStateOrNull = nullptr;
    return JobStatus::Ok;
}
} // namespace ludus::foundation::threading
