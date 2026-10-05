#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::foundation::threading
{
enum class JobStatus : uint8
{
    Ok,
    InvalidArgument,
    NotInitialized,
    AlreadyInitialized,
    OutOfMemory,
    CapacityExceeded,
    InvalidHandle,
    DuplicateDependency,
    CycleDetected,
    NotSealed,
    Busy,
    InsideJob,
    Unsupported,
    ThreadCreationFailed,
    IdentityExhausted,
};

enum class JobOutcome : uint8
{
    Pending,
    Running,
    Succeeded,
    Failed,
    Cancelled,
    Blocked,
};

// Only Succeeded, Failed, and Cancelled are valid callback return values.
using JobFunction = JobOutcome (*)(void* context) noexcept;

struct JobHandle
{
    uint64 GraphId = 0;
    uint64 Generation = 0;
    uint32 Index = 0;
};

struct JobGraphConfig
{
    uint32 MaxJobs = 256;
    uint32 MaxDependencies = 1024;
};

struct JobSystemConfig
{
    // Explicit budget: excludes the calling thread, which may help in Wait.
    // Zero selects the serial executor; browser builds require zero.
    uint32 WorkerCount = 0;
};

struct JobSnapshot
{
    uint32 Pending = 0;
    uint32 Running = 0;
    uint32 Succeeded = 0;
    uint32 Failed = 0;
    uint32 Cancelled = 0;
    uint32 Blocked = 0;
};

namespace detail
{
struct GraphState;
struct SystemState;
} // namespace detail

class JobSystem;

// Build/reset on one owner thread. All contexts and their inputs/outputs must
// outlive Wait. Graph storage is fixed at Initialize and never grows at run time.
class JobGraph final
{
public:
    JobGraph() noexcept = default;
    ~JobGraph() noexcept;
    JobGraph(const JobGraph&) = delete;
    JobGraph& operator=(const JobGraph&) = delete;
    JobGraph(JobGraph&&) = delete;
    JobGraph& operator=(JobGraph&&) = delete;

    [[nodiscard]] JobStatus Initialize(JobGraphConfig config = {}) noexcept;
    [[nodiscard]] JobStatus Add(JobFunction function, void* context, JobHandle& handle) noexcept;
    [[nodiscard]] JobStatus DependsOn(JobHandle dependent, JobHandle prerequisite) noexcept;
    [[nodiscard]] JobStatus Seal() noexcept;
    // Wait must release a submitted graph before resetting or editing it.
    [[nodiscard]] JobStatus Reset() noexcept;
    [[nodiscard]] JobStatus GetOutcome(JobHandle handle, JobOutcome& outcome) const noexcept;

private:
    friend class JobSystem;
    detail::GraphState* mState = nullptr;
};

// Lifecycle and Submit/Wait are serialized by the application owner. Snapshot
// and Cancel may be called concurrently, while the system remains initialized.
// One graph may be in flight per system; unrelated systems have independent pools.
// Callbacks must run to completion, never block on jobs/I/O, and never call a
// system lifecycle method. Wait helps by default; pass false to only wait.
class JobSystem final
{
public:
    JobSystem() noexcept = default;
    ~JobSystem() noexcept;
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    [[nodiscard]] JobStatus Initialize(JobSystemConfig config = {}) noexcept;
    [[nodiscard]] JobStatus Submit(JobGraph& graph) noexcept;
    [[nodiscard]] JobStatus Wait(JobOutcome& outcome, bool help = true) noexcept;
    [[nodiscard]] JobStatus Cancel() noexcept;
    [[nodiscard]] JobStatus Snapshot(JobSnapshot& snapshot) const noexcept;
    // Drains accepted work, wakes sleepers, and joins every started worker.
    [[nodiscard]] JobStatus Shutdown() noexcept;

private:
    detail::SystemState* mState = nullptr;
};
} // namespace ludus::foundation::threading
