#pragma once
#include "level.h"
#include <ludus/foundation/base/core.h>
#include <ludus/gameplay/world/component_pool.hpp>
#include <ludus/gameplay/world/entity_registry.h>
#include <span>
namespace ludus::world_demo
{
using gameplay::world::EntityId;
using gameplay::world::Status;
struct Transform final
{
    Vec2 Current{};
    Vec2 Previous{};
    float32 Rotation = 0;
    Vec2 Scale{1, 1};
};
struct Motion final
{
    Vec2 Velocity{};
    float32 Speed = 0;
    uint64 Revision = 0;
    uint64 PendingRequest = 0;
    bool Override = false;
};
struct Collider final
{
    Vec2 HalfExtent{};
};
struct Visual final
{
    Vec2 HalfExtent{};
    float32 Color[4] = {};
};
struct Health final
{
    uint32 Current = 0;
};
struct PlayerState final
{
    uint64 AttackReady = 0;
};
enum class ActionPhase : uint8
{
    SelectTarget,
    Approach,
    Attack,
    Recover
};
struct EnemyState final
{
    EntityId Target{};
    uint64 WakeTick = 0;
    ActionPhase Phase = ActionPhase::SelectTarget;
};
struct ExitState final
{
    Name NextLevel{};
    bool Entered = false;
    bool Touching = false;
};
struct TickInput final
{
    Vec2 Axis{};
    bool Held = false;
    bool Pressed = false;
    bool Released = false;
    bool Cancelled = false;
};
class InputBridge final
{
public:
    void Sample(TickInput input) noexcept;
    void Cancel() noexcept;
    [[nodiscard]] TickInput Consume() noexcept;

private:
    TickInput mPending;
    bool mGate = false;
};
enum class EventKind : uint8
{
    Damage,
    Death,
    Exit,
    Spawn
};
struct PresentationEvent final
{
    uint64 World = 0;
    uint64 Tick = 0;
    uint64 Sequence = 0;
    EventKind Kind = EventKind::Damage;
    Vec2 Anchor{};
};
struct Draw final
{
    Vec2 Position{};
    Vec2 HalfExtent{};
    float32 Rotation = 0;
    float32 Color[4] = {};
};
struct RenderFrame final
{
    Draw Draws[64] = {};
    usize Count = 0;
    Vec2 Camera{};
    float32 VerticalExtent = 14;
    float32 MinimumWidth = 24;
    uint64 Tick = 0;
};
struct SpawnOutcome final
{
    uint64 Request = 0;
    EntityId Entity{};
};
enum class CompletionState : uint8
{
    Ready,
    Cancelled,
    Failed
};
struct Completion final
{
    EntityId Entity{};
    uint64 Request = 0;
    uint64 Revision = 0;
    Vec2 Velocity{};
    CompletionState State = CompletionState::Ready;
};
enum class ContactPhase : uint8
{
    Enter,
    Stay,
    Exit
};
struct ContactEvent final
{
    EntityId First;
    EntityId Second;
    ContactPhase Kind = ContactPhase::Enter;
};
enum class Phase : uint8
{
    Construction,
    BeginTick,
    Intent,
    Motion,
    Gameplay,
    Commit,
    Idle,
    Faulted
};
struct TickTrace final
{
    uint64 Tick = 0;
    TickInput Input;
    Phase LastPhase = Phase::Idle;
    Status Result = Status::Success;
    usize Commands = 0;
    usize Contacts = 0;
    usize Presentation = 0;
    usize Completions = 0;
};
class GameWorld final
{
public:
    [[nodiscard]] Status Prepare(const Level& level, uint32 capacity = 48) noexcept;
    [[nodiscard]] Status BuildNext() noexcept;
    [[nodiscard]] bool IsBuilt() const noexcept
    {
        return mBuilt == mLevel.EntityCount && mRegistry.GetWorld() != 0;
    }
    [[nodiscard]] Status RunTick(TickInput input) noexcept;
    [[nodiscard]] Status QueueSpawn(Recipe recipe, uint64& request) noexcept;
    [[nodiscard]] Status QueueDestroy(EntityId entity) noexcept;
    [[nodiscard]] Status SubmitCompletion(Completion completion) noexcept;
    [[nodiscard]] Status BeginRequest(EntityId entity, Completion& ticket) noexcept;
    [[nodiscard]] bool Extract(float32 alpha, RenderFrame& frame) const noexcept;
    [[nodiscard]] EntityId FindAuthored(std::string_view id) const noexcept;
    [[nodiscard]] const gameplay::world::EntityRegistry& Registry() const noexcept
    {
        return mRegistry;
    }
    [[nodiscard]] const gameplay::world::ComponentPool<Transform>& Transforms() const noexcept
    {
        return mTransforms;
    }
    [[nodiscard]] const gameplay::world::ComponentPool<EnemyState>& Enemies() const noexcept
    {
        return mEnemies;
    }
    [[nodiscard]] const gameplay::world::ComponentPool<Health>& HealthValues() const noexcept
    {
        return mHealth;
    }
    [[nodiscard]] std::span<const PresentationEvent> GetOutbox() const noexcept
    {
        return {mOutbox, mOutboxCount};
    }
    [[nodiscard]] std::span<const SpawnOutcome> GetSpawnOutcomes() const noexcept
    {
        return {mOutcomes, mOutcomeCount};
    }
    void ConsumeOutbox() noexcept
    {
        mOutboxCount = 0;
    }
    [[nodiscard]] std::span<const TickTrace> GetTraceRing() const noexcept
    {
        return {mTrace, mTraceCount};
    }
    [[nodiscard]] std::span<const ContactEvent> GetContacts() const noexcept
    {
        return {mContacts, mContactCount};
    }
    [[nodiscard]] uint64 GetTick() const noexcept
    {
        return mTick;
    }
    [[nodiscard]] uint64 GetReplayHash() const noexcept;
    [[nodiscard]] Phase GetPhase() const noexcept
    {
        return mPhase;
    }
    [[nodiscard]] const Name& GetNextLevel() const noexcept
    {
        return mNextLevel;
    }
    [[nodiscard]] uint64 GetStaleCompletions() const noexcept
    {
        return mStaleCompletions;
    }
    [[nodiscard]] uint64 GetDroppedPresentation() const noexcept
    {
        return mDroppedPresentation;
    }

private:
    enum class CommandKind : uint8
    {
        Spawn,
        Destroy
    };
    struct Command final
    {
        CommandKind Kind = CommandKind::Spawn;
        Recipe Spawn{};
        EntityId Entity{};
        uint64 Request = 0;
    };
    [[nodiscard]] Status Spawn(const Recipe& recipe, EntityId& output) noexcept;
    [[nodiscard]] Status Commit() noexcept;
    void Intent(TickInput input) noexcept;
    void MoveAndCollide() noexcept;
    [[nodiscard]] Status Gameplay(TickInput input, Name& nextLevel) noexcept;
    void Stage(EventKind kind, Vec2 anchor) noexcept;
    [[nodiscard]] Status Damage(EntityId entity) noexcept;
    gameplay::world::EntityRegistry mRegistry;
    gameplay::world::ComponentPool<Transform> mTransforms;
    gameplay::world::ComponentPool<Motion> mMotion;
    gameplay::world::ComponentPool<Collider> mColliders;
    gameplay::world::ComponentPool<Visual> mVisuals;
    gameplay::world::ComponentPool<Health> mHealth;
    gameplay::world::ComponentPool<PlayerState> mPlayers;
    gameplay::world::ComponentPool<EnemyState> mEnemies;
    gameplay::world::ComponentPool<ExitState> mExits;
    Level mLevel;
    EntityId mAuthored[32] = {};
    EntityId mPlayer;
    usize mBuilt = 0;
    Command mCommands[32] = {};
    usize mCommandCount = 0;
    SpawnOutcome mOutcomes[32] = {};
    usize mOutcomeCount = 0;
    PresentationEvent mStaged[64] = {};
    usize mStagedCount = 0;
    PresentationEvent mOutbox[64] = {};
    usize mOutboxCount = 0;
    struct RequestLease final
    {
        uint64 Id = 0;
        EntityId Entity;
        uint64 Revision = 0;
        uint64 CreationSequence = 0;
        bool Completed = false;
    };
    RequestLease mRequests[16] = {};
    Completion mCompletions[16] = {};
    usize mCompletionCount = 0;
    ContactEvent mContacts[32] = {};
    usize mContactCount = 0;
    TickTrace mTrace[64] = {};
    usize mTraceCount = 0;
    usize mTraceNext = 0;
    uint64 mNextRequest = 1;
    uint64 mTick = 0;
    uint64 mStaleCompletions = 0;
    uint64 mDroppedPresentation = 0;
    Phase mPhase = Phase::Construction;
    Name mNextLevel;
};
enum class Mode : uint8
{
    Playing,
    Paused,
    Faulted
};
class TickDriver final
{
public:
    [[nodiscard]] Status Advance(GameWorld& world, float64 delta, TickInput input, bool visible = true) noexcept;
    [[nodiscard]] Status Step(GameWorld& world) noexcept;
    void SetMode(Mode mode) noexcept;
    void Reset() noexcept;
    [[nodiscard]] Mode GetMode() const noexcept
    {
        return mMode;
    }
    [[nodiscard]] float32 GetAlpha() const noexcept
    {
        return mMode == Mode::Playing ? static_cast<float32>(mDebt * 60.0) : 1.0F;
    }
    [[nodiscard]] float64 GetDiscardedTime() const noexcept
    {
        return mDiscarded;
    }

private:
    InputBridge mInput;
    float64 mDebt = 0;
    float64 mDiscarded = 0;
    Mode mMode = Mode::Playing;
};
enum class LoadStage : uint8
{
    Empty,
    Preparing,
    Building,
    Ready,
    Failed,
    Cancelled
};
class Session final
{
public:
    [[nodiscard]] LevelResult RequestLoad(std::string_view source) noexcept;
    [[nodiscard]] Status PollLoad() noexcept;
    [[nodiscard]] bool Activate() noexcept;
    void CancelLoad() noexcept;
    [[nodiscard]] GameWorld& World() noexcept
    {
        return mActive;
    }
    [[nodiscard]] const GameWorld& World() const noexcept
    {
        return mActive;
    }
    [[nodiscard]] LoadStage GetLoadStage() const noexcept
    {
        return mStage;
    }
    [[nodiscard]] uint64 GetTransition() const noexcept
    {
        return mTransition;
    }
    TickDriver Driver;

private:
    GameWorld mActive;
    GameWorld mCandidate;
    Level mDefinition;
    LoadStage mStage = LoadStage::Empty;
    uint64 mTransition = 0;
};
} // namespace ludus::world_demo
