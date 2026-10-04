#pragma once

#include <ludus/foundation/base/types.h>

#include <atomic>
#include <span>
#include <string_view>

namespace ludus::smoke::lifecycle
{
inline constexpr foundation::usize InvalidNode = ~foundation::usize{0};

enum class StartStatus : foundation::uint8
{
    Ready,
    Pending,
    Failed
};
enum class StopStatus : foundation::uint8
{
    Stopped,
    Pending,
    Unsafe
};
struct StartResult
{
    StartStatus Status = StartStatus::Ready;
    foundation::uint32 Error = 0;
    foundation::uint32 WaitReason = 0;
};
struct StopResult
{
    StopStatus Status = StopStatus::Stopped;
    foundation::uint32 Error = 0;
    foundation::uint32 WaitReason = 0;
};
struct Node
{
    // Dense IDs identify records; array order is the authored execution order.
    foundation::usize Id = InvalidNode;
    std::string_view Name;
    void* Context = nullptr;
    std::span<const foundation::usize> Providers;
    StartResult (*BeginStart)(void*) noexcept = nullptr;
    StartResult (*PollStart)(void*) noexcept = nullptr;
    StopResult (*BeginStop)(void*) noexcept = nullptr;
    StopResult (*PollStop)(void*) noexcept = nullptr;
};
enum class NodeState : foundation::uint8
{
    Dormant,
    Starting,
    Ready,
    Failed,
    Stopping,
    Stopped,
    CleanupBlocked
};
struct NodeRecord
{
    foundation::usize Position = InvalidNode;
    NodeState State = NodeState::Dormant;
    foundation::uint32 Error = 0;
    foundation::uint32 WaitReason = 0;
};
enum class State : foundation::uint8
{
    Idle,
    Starting,
    Ready,
    Stopping,
    Stopped,
    CleanupBlocked
};
enum class Outcome : foundation::uint8
{
    None,
    Completed,
    Cancelled,
    StartFailed,
    RuntimeFailed
};
enum class PlanError : foundation::uint8
{
    None,
    Capacity,
    InvalidId,
    DuplicateId,
    MissingName,
    MissingCallback,
    InvalidProvider,
    ProviderOrder
};
struct PlanDiagnostic
{
    PlanError Error = PlanError::None;
    foundation::usize NodeId = InvalidNode;
    foundation::usize ProviderId = InvalidNode;
};
struct Failure
{
    foundation::usize NodeId = InvalidNode;
    foundation::uint32 Error = 0;
};
enum class BeginStatus : foundation::uint8
{
    Started,
    Busy,
    InvalidPlan,
    GenerationExhausted
};

// Single owner-thread coordinator. Only RequestStop is cross-thread safe.
// Plan, names, contexts, records and journal must outlive this runner and all
// attempted nodes. Do not mutate the plan while an attempt is active. Callbacks
// must return promptly; Pending is polled on a later owner-loop invocation.
// This private prototype has no allocation, discovery, scheduler or SDK API.
class Runner final
{
public:
    Runner(std::span<const Node> plan, std::span<NodeRecord> records, std::span<foundation::usize> journal) noexcept;
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;
    Runner(Runner&&) = delete;
    Runner& operator=(Runner&&) = delete;

    [[nodiscard]] BeginStatus Begin() noexcept;
    State Advance(foundation::usize callbackBudget = 1) noexcept;
    void RequestStop() noexcept;
    // Runtime failures and deadline decisions are supplied by the owner. Neither
    // cancellation nor a deadline is evidence that a node has retired.
    bool ReportFailure(foundation::usize nodeId, foundation::uint32 error) noexcept;
    bool BlockCleanup(foundation::uint32 error) noexcept;
    bool ResumeCleanup() noexcept;

    [[nodiscard]] State GetState() const noexcept
    {
        return mState;
    }
    [[nodiscard]] Outcome GetOutcome() const noexcept
    {
        return mOutcome;
    }
    [[nodiscard]] Failure GetFailure() const noexcept
    {
        return mFailure;
    }
    [[nodiscard]] Failure GetCleanupFailure() const noexcept
    {
        return mCleanupFailure;
    }
    [[nodiscard]] PlanDiagnostic GetPlanDiagnostic() const noexcept
    {
        return mPlanDiagnostic;
    }
    [[nodiscard]] foundation::uint64 GetGeneration() const noexcept
    {
        return mGeneration;
    }
    [[nodiscard]] foundation::usize GetAttemptedCount() const noexcept
    {
        return mAttempted;
    }
    [[nodiscard]] std::span<const Node> GetPlan() const noexcept
    {
        return mPlan;
    }
    [[nodiscard]] std::span<const NodeRecord> GetRecords() const noexcept
    {
        // Remains safe to inspect after rejecting undersized storage.
        return mRecords.size() > mPlan.size() ? mRecords.first(mPlan.size()) : mRecords;
    }

private:
    bool Validate() noexcept;
    void ObserveStopRequest() noexcept;
    void StartOne() noexcept;
    void StopOne() noexcept;

    std::span<const Node> mPlan;
    std::span<NodeRecord> mRecords;
    std::span<foundation::usize> mJournal;
    std::atomic<bool> mStopRequested{false};
    State mState = State::Idle;
    Outcome mOutcome = Outcome::None;
    Failure mFailure;
    Failure mCleanupFailure;
    PlanDiagnostic mPlanDiagnostic;
    foundation::uint64 mGeneration = 0;
    foundation::usize mNext = 0;
    foundation::usize mAttempted = 0;
    bool mAdvancing = false;
};
} // namespace ludus::smoke::lifecycle
