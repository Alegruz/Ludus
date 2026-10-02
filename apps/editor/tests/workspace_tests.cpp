// Offscreen tests for the WorkspaceState transition function and capabilities.
// These exercise the state machine with no widgets and no tools (design 3, 6).

#include "internal/workspace.h"

#include <catch2/catch_test_macros.hpp>

using namespace ludus::editor;

namespace
{
WorkspaceState LoadedState()
{
    WorkspaceState state;
    state.Document = DocumentState::ProjectLoaded;
    state.HasSaved = true;
    state.DescriptorPath = QStringLiteral("/tmp/ludus.project.json");
    state.SavedDigest = QStringLiteral("abc");
    ProjectDescriptor descriptor;
    descriptor.Name = QStringLiteral("demo");
    descriptor.Preset = QStringLiteral("linux-clang-debug");
    descriptor.Target = QStringLiteral("app");
    descriptor.SourceDir = QStringLiteral(".");
    descriptor.RunCwd = QStringLiteral(".");
    state.Saved = descriptor;
    state.Draft = descriptor;
    return state;
}
} // namespace

TEST_CASE("No project forbids all operations", "[editor][workspace]")
{
    WorkspaceState state;
    const Capabilities caps = ComputeCapabilities(state);
    CHECK_FALSE(caps.CanConfigure);
    CHECK_FALSE(caps.CanBuild);
    CHECK_FALSE(caps.CanBuildRun);
    CHECK_FALSE(caps.CanSave);
    CHECK(caps.CanOpen);
}

TEST_CASE("Dirty document blocks build/run but allows configure and save", "[editor][workspace]")
{
    WorkspaceState state = LoadedState();
    state.Draft.Name = QStringLiteral("edited"); // now dirty
    REQUIRE(state.Dirty());
    const Capabilities caps = ComputeCapabilities(state);
    CHECK(caps.CanConfigure);
    CHECK(caps.CanSave);
    CHECK_FALSE(caps.CanBuild);
    CHECK_FALSE(caps.CanBuildRun);
}

TEST_CASE("Clean saved document permits build and run", "[editor][workspace]")
{
    WorkspaceState state = LoadedState();
    REQUIRE_FALSE(state.Dirty());
    const Capabilities caps = ComputeCapabilities(state);
    CHECK(caps.CanBuild);
    CHECK(caps.CanBuildRun);
}

TEST_CASE("Exactly one owned operation; duplicate starts cannot create a second job",
          "[editor][workspace]")
{
    WorkspaceState state = LoadedState();
    REQUIRE(CanStartJob(state, ActionKind::Configure));
    const WorkspaceState running = BeginJob(state, ActionKind::Configure);
    CHECK(running.ActiveJob == 1);
    CHECK(running.OperationPhase == Phase::Starting);
    // While a job is owned, no new job may start.
    CHECK_FALSE(CanStartJob(running, ActionKind::Configure));
    CHECK_FALSE(CanStartJob(running, ActionKind::Build));
}

TEST_CASE("Only runtime confirmation moves Launching to Running", "[editor][workspace]")
{
    WorkspaceState state = LoadedState();
    state = BeginJob(state, ActionKind::BuildRun);
    const ludus::foundation::uint64 job = state.ActiveJob;
    state = ApplyPhaseEvent(state, job, Phase::Configuring, false);
    CHECK(state.OperationPhase == Phase::Configuring);
    state = ApplyPhaseEvent(state, job, Phase::Building, false);
    CHECK(state.OperationPhase == Phase::Building);
    state = ApplyPhaseEvent(state, job, Phase::Launching, false);
    CHECK(state.OperationPhase == Phase::Launching);
    // A bare phase event cannot assert Running.
    const WorkspaceState notRunning = ApplyPhaseEvent(state, job, Phase::Running, false);
    CHECK(notRunning.OperationPhase == Phase::Launching);
    // Only a runtime-started confirmation moves to Running.
    const WorkspaceState running = ApplyPhaseEvent(state, job, Phase::Running, true);
    CHECK(running.OperationPhase == Phase::Running);
}

TEST_CASE("Stop latched before spawn prevents a later launch", "[editor][workspace]")
{
    WorkspaceState state = LoadedState();
    state = BeginJob(state, ActionKind::BuildRun);
    const ludus::foundation::uint64 job = state.ActiveJob;
    state = ApplyPhaseEvent(state, job, Phase::Configuring, false);
    state = LatchStop(state);
    CHECK(state.OperationPhase == Phase::Stopping);
    CHECK(state.StopLatched);
    // A late build-success / runtime confirmation must not launch after Stop.
    const WorkspaceState afterLateRuntime = ApplyPhaseEvent(state, job, Phase::Running, true);
    CHECK(afterLateRuntime.OperationPhase == Phase::Stopping);
}

TEST_CASE("Stale events for a non-current job are ignored", "[editor][workspace]")
{
    WorkspaceState state = LoadedState();
    state = BeginJob(state, ActionKind::Configure);
    const WorkspaceState unchanged = ApplyPhaseEvent(state, 999, Phase::Configuring, false);
    CHECK(unchanged.OperationPhase == state.OperationPhase);
}

TEST_CASE("A result preserves failure and is not erased by becoming idle", "[editor][workspace]")
{
    WorkspaceState state = LoadedState();
    state = BeginJob(state, ActionKind::Build);
    const ludus::foundation::uint64 job = state.ActiveJob;
    LastResult result;
    result.Kind = Outcome::Failed;
    result.Code = ResultCode::BuildFailed;
    result.Message = QStringLiteral("compile error");
    result.CleanupConfirmed = true;
    state = ApplyResult(state, job, result);
    CHECK(state.OperationPhase == Phase::Idle);
    CHECK(state.ActiveJob == 0);
    CHECK(state.Result.Kind == Outcome::Failed);
    CHECK(state.Result.Code == ResultCode::BuildFailed);
    // A duplicate terminal result for the retired job is ignored.
    LastResult second = result;
    second.Code = ResultCode::Ok;
    const WorkspaceState afterDuplicate = ApplyResult(state, job, second);
    CHECK(afterDuplicate.Result.Code == ResultCode::BuildFailed);
}

TEST_CASE("Unconfirmed cleanup becomes CleanupUnknown, not success", "[editor][workspace]")
{
    WorkspaceState state = LoadedState();
    state = BeginJob(state, ActionKind::BuildRun);
    const ludus::foundation::uint64 job = state.ActiveJob;
    LastResult result;
    result.Kind = Outcome::Success;
    result.Code = ResultCode::Ok;
    result.CleanupConfirmed = false; // supervisor loss / unverified cleanup
    state = ApplyResult(state, job, result);
    CHECK(state.OperationPhase == Phase::CleanupUnknown);
    // CleanupUnknown forbids starting a new operation.
    CHECK_FALSE(CanStartJob(state, ActionKind::Configure));
}

TEST_CASE("Job id counter advances and never silently reuses", "[editor][workspace]")
{
    WorkspaceState state = LoadedState();
    state = BeginJob(state, ActionKind::Configure);
    const ludus::foundation::uint64 first = state.ActiveJob;
    LastResult ok;
    ok.Kind = Outcome::Success;
    ok.Code = ResultCode::Ok;
    ok.CleanupConfirmed = true;
    state = ApplyResult(state, first, ok);
    state = BeginJob(state, ActionKind::Configure);
    CHECK(state.ActiveJob == first + 1);
}
