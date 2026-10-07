#pragma once
#include <ludus/foundation/base/core.h>

#include <ludus/foundation/base/byte_order.hpp>

#include <span>
#include <string_view>

#include "internal/runtime.h"
#include "internal/state.h"
#include "shared.h"

namespace ludus::s2
{
using namespace foundation;
using runtime::scripting::BreakpointSpec;
using runtime::scripting::Diagnostic;
using runtime::scripting::Program;
struct InvokeRequest
{
    uint32 Instance = 0;
    uint32 Amount = 1;
};
using runtime::scripting::ResumeMode;
using runtime::scripting::Runtime;
using runtime::scripting::Status;
struct Package
{
    const char* Key = nullptr;
    const char* Contract = nullptr;
    const char* Pin = nullptr;
    uint64 Revision = 0;
    uint32 Schema = 0;
    uint32 StateMax = 0;
    const Program* Programs = nullptr;
    usize Count = 0;
    int32 FirstLine = 0;
};
enum class Replacement : uint8
{
    Ready,
    Rejected,
    NotSafePoint,
    Stale,
    Committed
};
// One owner-thread cohort, two declared instances, and a whole-VM swap. The
// trusted compiled catalog owns code/maps until native-module retirement.
class Session final
{
public:
    [[nodiscard]] bool Initialize(const Package& package) noexcept;
    // Private authoring owner boundary: every entry must come from the paired
    // trusted cooker. Catalog, package and code storage are borrowed until Close;
    // the active/staged entries must stay immutable. Never admit browser bytes
    // or authenticate a package by its checksum alone.
    [[nodiscard]] bool Initialize(const Package& package, std::span<const Package* const> admitted) noexcept;
    [[nodiscard]] Status Begin(InvokeRequest request = {}) noexcept;
    [[nodiscard]] Status Resume(ResumeMode mode) noexcept;
    [[nodiscard]] Replacement Prepare(const Package& package, std::string_view expected) noexcept;
    [[nodiscard]] Replacement Commit(std::string_view expected) noexcept;
    void Discard() noexcept;
    void Close() noexcept;
    [[nodiscard]] int32 Breakpoint(BreakpointSpec point) noexcept;
    [[nodiscard]] bool SafePoint() const noexcept;
    [[nodiscard]] bool Checkpoint(std::span<uint8> output, usize& written) const noexcept;
    [[nodiscard]] bool Restore(std::span<const uint8> input) noexcept;
    // Same bounded extension used by browser exports and the GameHost callback.
    [[nodiscard]] bool Control(std::string_view input, std::span<uint8> output, usize& written) noexcept;
    [[nodiscard]] const Package* Active() const noexcept
    {
        return mActive;
    }
    [[nodiscard]] Runtime& Vm() noexcept
    {
        return mVms[mBank];
    }
    [[nodiscard]] const Diagnostic& Last() const noexcept
    {
        return mLast;
    }
    [[nodiscard]] s1::World& World() noexcept
    {
        return mWorld;
    }
    [[nodiscard]] bool Pending() const noexcept
    {
        return mPending;
    }
    [[nodiscard]] usize Heap() const noexcept
    {
        return mVms[0].GetMemory().Live + mVms[1].GetMemory().Live;
    }
    [[nodiscard]] usize CandidateAttempts() const noexcept
    {
        return mVms[1 - mBank].GetMemory().Attempts;
    }
    void FailCandidateAllocation(usize attempt) noexcept
    {
        mVms[1 - mBank].SetAllocationFailure(attempt);
    }

private:
    [[nodiscard]] Status Finish(Status status) noexcept;
    Runtime mVms[2];
    s1::World mWorld;
    s1::Transaction mCandidate;
    s1::State mMigrated[2];
    Diagnostic mLast;
    const Package* mActive = nullptr;
    const Package* mStaged = nullptr;
    std::span<const Package* const> mCatalog;
    uint32 mBank = 0;
    bool mPending = false;
    uint64 mPreparedTick = 0;
    uint64 mPreparedExecution = 0;
};
} // namespace ludus::s2
