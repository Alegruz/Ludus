#include "internal/world.h"

#include <ludus/foundation/profiling/profiling.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
namespace ludus::world_demo
{
namespace
{
constexpr float64 TickDuration = 1.0 / 60.0;
bool Finite(Vec2 value) noexcept
{
    return std::isfinite(value.X) && std::isfinite(value.Y);
}
float32 Distance(Vec2 first, Vec2 second) noexcept
{
    return std::hypot(first.X - second.X, first.Y - second.Y);
}
bool Overlap(Vec2 first, Vec2 a, Vec2 second, Vec2 b) noexcept
{
    return std::abs(first.X - second.X) < a.X + b.X && std::abs(first.Y - second.Y) < a.Y + b.Y;
}
} // namespace
void InputBridge::Sample(TickInput input) noexcept
{
    if (input.Cancelled)
    {
        Cancel();
        return;
    }
    if (mGate)
    {
        if (!input.Held && input.Axis.X == 0 && input.Axis.Y == 0)
        {
            mGate = false;
        }
        return;
    }
    mPending.Axis = input.Axis;
    mPending.Held = input.Held;
    mPending.Pressed |= input.Pressed;
    mPending.Released |= input.Released;
}
void InputBridge::Cancel() noexcept
{
    mPending = {};
    mPending.Cancelled = true;
    mGate = true;
}
TickInput InputBridge::Consume() noexcept
{
    const auto result = mPending;
    mPending.Pressed = false;
    mPending.Released = false;
    mPending.Cancelled = false;
    return result;
}
Status GameWorld::Prepare(const Level& level, uint32 capacity) noexcept
{
    if (ValidateLevel(level) != LevelError::None || capacity < level.EntityCount || capacity > 48)
    {
        return Status::InvalidConfiguration;
    }
    auto status = mRegistry.TryInitialize(capacity);
    if (status != Status::Success)
    {
        return status;
    }
    status = mTransforms.TryInitialize(mRegistry);
    if (status != Status::Success)
    {
        return status;
    }
    status = mMotion.TryInitialize(mRegistry);
    if (status != Status::Success)
    {
        return status;
    }
    status = mColliders.TryInitialize(mRegistry);
    if (status != Status::Success)
    {
        return status;
    }
    status = mVisuals.TryInitialize(mRegistry);
    if (status != Status::Success)
    {
        return status;
    }
    status = mHealth.TryInitialize(mRegistry);
    if (status != Status::Success)
    {
        return status;
    }
    status = mPlayers.TryInitialize(mRegistry);
    if (status != Status::Success)
    {
        return status;
    }
    status = mEnemies.TryInitialize(mRegistry);
    if (status != Status::Success)
    {
        return status;
    }
    status = mExits.TryInitialize(mRegistry);
    if (status != Status::Success)
    {
        return status;
    }
    mLevel = level;
    std::sort(mLevel.Entities, mLevel.Entities + mLevel.EntityCount, [](const Recipe& first, const Recipe& second) {
        return first.Id.View() < second.Id.View();
    });
    return Status::Success;
}
Status GameWorld::Spawn(const Recipe& recipe, EntityId& output) noexcept
{
    auto status = mRegistry.TryCreate(output);
    if (status != Status::Success)
    {
        return status;
    }
    status =
        mTransforms.TryInsert(mRegistry, output, {recipe.Position, recipe.Position, recipe.Rotation, recipe.Scale});
    if (status != Status::Success)
    {
        return status;
    }
    const Vec2 extent{recipe.HalfExtent.X * recipe.Scale.X, recipe.HalfExtent.Y * recipe.Scale.Y};
    status = mColliders.TryInsert(mRegistry, output, {extent});
    if (status != Status::Success)
    {
        return status;
    }
    Visual visual{ .HalfExtent = extent };
    visual.Color[3] = 1;
    if (recipe.Type == Kind::Player)
    {
        visual.Color[0] = 1;
        visual.Color[1] = 0.65F;
        visual.Color[2] = 0.15F;
    }
    else if (recipe.Type == Kind::Enemy)
    {
        visual.Color[0] = 0.9F;
        visual.Color[1] = 0.15F;
        visual.Color[2] = 0.2F;
    }
    else
    {
        visual.Color[0] = 0.2F;
        visual.Color[1] = 0.9F;
        visual.Color[2] = 0.5F;
    }
    status = mVisuals.TryInsert(mRegistry, output, visual);
    if (status != Status::Success)
    {
        return status;
    }
    if (recipe.Type == Kind::Exit)
    {
        return mExits.TryInsert(mRegistry, output, {recipe.NextLevel});
    }
    status = mMotion.TryInsert(mRegistry, output, { .Speed = recipe.Speed });
    if (status != Status::Success)
    {
        return status;
    }
    status = mHealth.TryInsert(mRegistry, output, {recipe.Health});
    if (status != Status::Success)
    {
        return status;
    }
    if (recipe.Type == Kind::Player)
    {
        mPlayer = output;
        return mPlayers.TryInsert(mRegistry, output, {});
    }
    return mEnemies.TryInsert(mRegistry, output, {mPlayer});
}
Status GameWorld::BuildNext() noexcept
{
    if (mPhase != Phase::Construction || mRegistry.GetWorld() == 0 || IsBuilt())
    {
        return Status::InvalidPhase;
    }
    auto status = Spawn(mLevel.Entities[mBuilt], mAuthored[mBuilt]);
    if (status != Status::Success)
    {
        return status;
    }
    ++mBuilt;
    if (IsBuilt())
    {
        for (usize index = 0; index < mBuilt; ++index)
        {
            if (auto* enemy = mEnemies.Find(mAuthored[index]))
            {
                enemy->Target = FindAuthored(mLevel.Entities[index].Target.View());
            }
        }
        mRegistry.SetStructuralPhase(false);
        mPhase = Phase::Idle;
    }
    return Status::Success;
}
EntityId GameWorld::FindAuthored(std::string_view id) const noexcept
{
    for (usize index = 0; index < mBuilt; ++index)
    {
        if (mLevel.Entities[index].Id.View() == id)
        {
            return mAuthored[index];
        }
    }
    return {};
}
Status GameWorld::QueueDestroy(EntityId entity) noexcept
{
    if (mPhase != Phase::Gameplay && mPhase != Phase::BeginTick && mPhase != Phase::Idle)
    {
        return Status::InvalidPhase;
    }
    if (!mRegistry.IsAlive(entity))
    {
        return Status::InvalidEntity;
    }
    if (!mRegistry.IsActive(entity))
    {
        return Status::Success;
    }
    if (mCommandCount == 32)
    {
        return Status::CapacityExceeded;
    }
    mCommands[mCommandCount++] = { .Kind = CommandKind::Destroy, .Entity = entity };
    return mRegistry.MarkPendingDestroy(entity);
}
Status GameWorld::QueueSpawn(Recipe recipe, uint64& request) noexcept
{
    if (mPhase != Phase::Gameplay && mPhase != Phase::BeginTick && mPhase != Phase::Idle)
    {
        return Status::InvalidPhase;
    }
    // This game's runtime recipe is an additional guard targeting the existing
    // player. No cross-reference to another queued spawn is supported.
    if (recipe.Type != Kind::Enemy || !mRegistry.IsActive(mPlayer))
    {
        return Status::InvalidConfiguration;
    }
    Level validation = mLevel;
    if (validation.EntityCount == 32)
    {
        return Status::CapacityExceeded;
    }
    validation.Entities[validation.EntityCount++] = recipe;
    if (ValidateLevel(validation) != LevelError::None)
    {
        return Status::InvalidConfiguration;
    }
    if (mCommandCount == 32)
    {
        return Status::CapacityExceeded;
    }
    if (mNextRequest == std::numeric_limits<uint64>::max())
    {
        return Status::IdentityExhausted;
    }
    request = mNextRequest++;
    mCommands[mCommandCount++] = { .Kind = CommandKind::Spawn, .Spawn = recipe, .Request = request };
    return Status::Success;
}
Status GameWorld::BeginRequest(EntityId entity, Completion& ticket) noexcept
{
    if (mPhase != Phase::Idle)
    {
        return Status::InvalidPhase;
    }
    auto* motion = mMotion.Find(entity);
    if (!mRegistry.IsActive(entity) || motion == nullptr)
    {
        return Status::InvalidEntity;
    }
    if (mNextRequest == std::numeric_limits<uint64>::max() || motion->Revision == std::numeric_limits<uint64>::max())
    {
        return Status::IdentityExhausted;
    }
    RequestLease* lease = nullptr;
    for (auto& entry : mRequests)
    {
        if (entry.Id == 0)
        {
            lease = &entry;
            break;
        }
    }
    if (lease == nullptr)
    {
        return Status::CapacityExceeded;
    }
    const auto request = mNextRequest++;
    *lease =
    {
        .Id = request,
        .Entity = entity,
        .Revision = motion->Revision + 1,
        .CreationSequence = mRegistry.GetCreationSequence(entity),
    };
    motion->PendingRequest = request;
    ++motion->Revision;
    ticket = { .Entity = entity, .Request = request, .Revision = motion->Revision };
    return Status::Success;
}
Status GameWorld::SubmitCompletion(Completion completion) noexcept
{
    // Main-thread mailbox entry. A worker must hand an owned result to its own
    // synchronized transport; it may never call this or retain pool borrows.
    if (mPhase != Phase::Idle)
    {
        return Status::InvalidPhase;
    }
    if (completion.Entity.World != mRegistry.GetWorld())
    {
        ++mStaleCompletions;
        return Status::InvalidEntity;
    }
    if (completion.State != CompletionState::Ready && completion.State != CompletionState::Cancelled &&
        completion.State != CompletionState::Failed)
    {
        return Status::InvalidConfiguration;
    }
    if (!Finite(completion.Velocity) || std::abs(completion.Velocity.X) > 32 || std::abs(completion.Velocity.Y) > 32)
    {
        return Status::InvalidConfiguration;
    }
    RequestLease* lease = nullptr;
    for (auto& entry : mRequests)
    {
        if (entry.Id == completion.Request && entry.Id != 0)
        {
            lease = &entry;
            break;
        }
    }
    if (lease == nullptr || lease->Completed)
    {
        return Status::InvalidConfiguration;
    }
    if (lease->Entity != completion.Entity || lease->Revision != completion.Revision)
    {
        ++mStaleCompletions;
        return Status::InvalidEntity;
    }
    if (mCompletionCount == 16)
    {
        return Status::CapacityExceeded;
    }
    lease->Completed = true;
    mCompletions[mCompletionCount++] = completion;
    return Status::Success;
}
void GameWorld::Intent(TickInput input) noexcept
{
    LUDUS_PROFILE_SCOPE(WorldIntent);
    // Reads active identities, targets and positions. Writes velocities and AI
    // phase/deadline only; structural effects are recorded by Gameplay.
    if (auto* motion = mMotion.Find(mPlayer); motion != nullptr && mRegistry.IsActive(mPlayer))
    {
        if (!motion->Override)
        {
            motion->Velocity = {std::clamp(input.Axis.X, -1.0F, 1.0F) * motion->Speed,
                                std::clamp(input.Axis.Y, -1.0F, 1.0F) * motion->Speed};
        }
    }
    const auto owners = mEnemies.GetOwners();
    for (usize index = 0; index < owners.size(); ++index)
    {
        const auto entity = owners[index];
        if (!mRegistry.IsActive(entity))
        {
            continue;
        }
        auto& enemy = mEnemies.GetValues()[index];
        auto* motion = mMotion.Find(entity);
        if (!mRegistry.IsActive(enemy.Target))
        {
            motion->Velocity = {};
            enemy.Phase = ActionPhase::SelectTarget;
            continue;
        }
        if (motion->Override)
        {
            continue;
        }
        const auto* target = mTransforms.Find(enemy.Target);
        const auto* transform = mTransforms.Find(entity);
        const float32 distance = Distance(target->Current, transform->Current);
        if (mTick + 1 < enemy.WakeTick)
        {
            enemy.Phase = ActionPhase::Recover;
            motion->Velocity = {};
        }
        else if (distance < 0.9F)
        {
            enemy.Phase = ActionPhase::Attack;
            motion->Velocity = {};
        }
        else
        {
            enemy.Phase = ActionPhase::Approach;
            motion->Velocity = {(target->Current.X - transform->Current.X) / distance * motion->Speed,
                                (target->Current.Y - transform->Current.Y) / distance * motion->Speed};
        }
    }
}
void GameWorld::MoveAndCollide() noexcept
{
    LUDUS_PROFILE_SCOPE(WorldMotion);
    // Axis-separated swept box motion against static authored boxes; no gravity.
    // Authoritative positions are never interpolated or written by rendering.
    const auto owners = mMotion.GetOwners();
    for (usize index = 0; index < owners.size(); ++index)
    {
        const auto entity = owners[index];
        if (!mRegistry.IsActive(entity))
        {
            continue;
        }
        auto* transform = mTransforms.Find(entity);
        const auto half = mColliders.Find(entity)->HalfExtent;
        const auto velocity = mMotion.GetValues()[index].Velocity;
        for (usize axis = 0; axis < 2; ++axis)
        {
            Vec2 proposed = transform->Current;
            const float32 movement = (axis == 0 ? velocity.X : velocity.Y) / 60.0F;
            float32& position = axis == 0 ? proposed.X : proposed.Y;
            position += movement;
            for (usize boxIndex = 0; boxIndex < mLevel.CollisionCount; ++boxIndex)
            {
                const auto& box = mLevel.Collision[boxIndex];
                const float32 old = axis == 0 ? transform->Current.X : transform->Current.Y;
                const float32 size = axis == 0 ? half.X : half.Y;
                const float32 center = axis == 0 ? box.Center.X : box.Center.Y;
                const float32 extent = axis == 0 ? box.HalfExtent.X : box.HalfExtent.Y;
                const bool otherOverlap = axis == 0 ? std::abs(proposed.Y - box.Center.Y) < half.Y + box.HalfExtent.Y
                                                    : std::abs(proposed.X - box.Center.X) < half.X + box.HalfExtent.X;
                if (otherOverlap && movement > 0 && old + size <= center - extent && position + size > center - extent)
                {
                    position = center - extent - size;
                }
                if (otherOverlap && movement < 0 && old - size >= center + extent && position - size < center + extent)
                {
                    position = center + extent + size;
                }
            }
            position = std::clamp(position,
                                  (axis == 0 ? mLevel.Minimum.X + half.X : mLevel.Minimum.Y + half.Y),
                                  (axis == 0 ? mLevel.Maximum.X - half.X : mLevel.Maximum.Y - half.Y));
            transform->Current = proposed;
        }
    }
    mContactCount = 0;
    for (auto entity : mExits.GetOwners())
    {
        if (!mRegistry.IsActive(entity))
        {
            continue;
        }
        auto* exit = mExits.Find(entity);
        const bool touching = mRegistry.IsActive(mPlayer) && Overlap(mTransforms.Find(entity)->Current,
                                                                     mColliders.Find(entity)->HalfExtent,
                                                                     mTransforms.Find(mPlayer)->Current,
                                                                     mColliders.Find(mPlayer)->HalfExtent);
        if (touching || exit->Touching)
        {
            auto first = entity;
            auto second = mPlayer;
            if (mRegistry.GetCreationSequence(first) > mRegistry.GetCreationSequence(second))
            {
                std::swap(first, second);
            }
            mContacts[mContactCount++] = {first,
                                          second,
                                          touching ? (exit->Touching ? ContactPhase::Stay : ContactPhase::Enter)
                                                   : ContactPhase::Exit};
        }
        exit->Touching = touching;
    }
    std::sort(mContacts, mContacts + mContactCount, [&](const ContactEvent& first, const ContactEvent& second) {
        const auto a = mRegistry.GetCreationSequence(first.First);
        const auto b = mRegistry.GetCreationSequence(second.First);
        return a != b ? a < b
                      : mRegistry.GetCreationSequence(first.Second) < mRegistry.GetCreationSequence(second.Second);
    });
}
void GameWorld::Stage(EventKind kind, Vec2 anchor) noexcept
{
    if (mStagedCount == 64)
    {
        ++mDroppedPresentation;
        return;
    }
    mStaged[mStagedCount] = {mRegistry.GetWorld(), mTick + 1, static_cast<uint64>(mStagedCount), kind, anchor};
    ++mStagedCount;
}
Status GameWorld::Damage(EntityId entity) noexcept
{
    auto* health = mHealth.Find(entity);
    if (!mRegistry.IsActive(entity) || health == nullptr || health->Current == 0)
    {
        return Status::Success;
    }
    --health->Current;
    const Vec2 position = mTransforms.Find(entity)->Current;
    Stage(EventKind::Damage, position);
    if (health->Current == 0)
    {
        const auto status = QueueDestroy(entity);
        if (status != Status::Success)
        {
            return status;
        }
        Stage(EventKind::Death, position);
    }
    return Status::Success;
}
Status GameWorld::Gameplay(TickInput input, Name& nextLevel) noexcept
{
    LUDUS_PROFILE_SCOPE(WorldGameplay);
    // Damage order is player intent, then stable enemy creation sequence, then
    // exit triggers. Pending destruction immediately excludes later interactions.
    if (mRegistry.IsActive(mPlayer))
    {
        auto* player = mPlayers.Find(mPlayer);
        if (input.Pressed && !input.Cancelled && mTick + 1 >= player->AttackReady)
        {
            if (mTick > std::numeric_limits<uint64>::max() - 31)
            {
                return Status::IdentityExhausted;
            }
            player->AttackReady = mTick + 31;
            EntityId enemies[64] = {};
            usize count = 0;
            for (auto entity : mEnemies.GetOwners())
            {
                enemies[count++] = entity;
            }
            std::sort(enemies, enemies + count, [&](EntityId first, EntityId second) {
                return mRegistry.GetCreationSequence(first) < mRegistry.GetCreationSequence(second);
            });
            for (usize index = 0; index < count; ++index)
            {
                if (mRegistry.IsActive(enemies[index]) &&
                    Distance(mTransforms.Find(mPlayer)->Current, mTransforms.Find(enemies[index])->Current) < 2)
                {
                    const auto status = Damage(enemies[index]);
                    if (status != Status::Success)
                    {
                        return status;
                    }
                }
            }
        }
    }
    EntityId enemies[64] = {};
    usize count = 0;
    for (auto entity : mEnemies.GetOwners())
    {
        enemies[count++] = entity;
    }
    std::sort(enemies, enemies + count, [&](EntityId first, EntityId second) {
        return mRegistry.GetCreationSequence(first) < mRegistry.GetCreationSequence(second);
    });
    for (usize index = 0; index < count; ++index)
    {
        if (!mRegistry.IsActive(enemies[index]))
        {
            continue;
        }
        auto* enemy = mEnemies.Find(enemies[index]);
        if (enemy->Phase == ActionPhase::Attack && mRegistry.IsActive(enemy->Target))
        {
            if (mTick > std::numeric_limits<uint64>::max() - 31)
            {
                return Status::IdentityExhausted;
            }
            enemy->WakeTick = mTick + 31;
            enemy->Phase = ActionPhase::Recover;
            const auto status = Damage(enemy->Target);
            if (status != Status::Success)
            {
                return status;
            }
        }
    }
    for (const auto& contact : GetContacts())
    {
        if (contact.Kind != ContactPhase::Enter || !mRegistry.IsActive(contact.First) ||
            !mRegistry.IsActive(contact.Second))
        {
            continue;
        }
        const auto entity = mExits.Find(contact.First) != nullptr ? contact.First : contact.Second;
        auto* exit = mExits.Find(entity);
        if (exit != nullptr && !exit->Entered)
        {
            exit->Entered = true;
            if (nextLevel.View().empty())
            {
                nextLevel = exit->NextLevel;
            }
            Stage(EventKind::Exit, mTransforms.Find(entity)->Current);
        }
    }
    return Status::Success;
}
Status GameWorld::Commit() noexcept
{
    LUDUS_PROFILE_SCOPE(WorldCommit);
    usize creates = 0;
    usize releases = 0;
    for (usize index = 0; index < mCommandCount; ++index)
    {
        if (mCommands[index].Kind == CommandKind::Spawn)
        {
            ++creates;
        }
        else
        {
            ++releases;
        }
    }
    auto status = mRegistry.CheckStructuralBudget(creates, releases);
    if (status != Status::Success)
    {
        return status;
    }
    mRegistry.SetStructuralPhase(true);
    for (usize index = 0; index < mCommandCount; ++index)
    {
        const auto& command = mCommands[index];
        if (command.Kind == CommandKind::Spawn)
        {
            EntityId entity;
            status = Spawn(command.Spawn, entity);
            if (status != Status::Success)
            {
                mRegistry.SetStructuralPhase(false);
                return status;
            }
            mOutcomes[mOutcomeCount++] = {command.Request, entity};
            Stage(EventKind::Spawn, command.Spawn.Position);
        }
        else
        {
            const auto entity = command.Entity;
            if (entity == mPlayer)
            {
                for (auto& exit : mExits.GetValues())
                {
                    exit.Touching = false;
                }
            }
            const auto remove = [&]<typename T>(gameplay::world::ComponentPool<T>& pool) {
                if (pool.Find(entity) != nullptr)
                {
                    return pool.TryRemove(mRegistry, entity);
                }
                return Status::Success;
            };
            status = remove(mTransforms);
            if (status != Status::Success)
            {
                return status;
            }
            status = remove(mMotion);
            if (status != Status::Success)
            {
                return status;
            }
            status = remove(mColliders);
            if (status != Status::Success)
            {
                return status;
            }
            status = remove(mVisuals);
            if (status != Status::Success)
            {
                return status;
            }
            status = remove(mHealth);
            if (status != Status::Success)
            {
                return status;
            }
            status = remove(mPlayers);
            if (status != Status::Success)
            {
                return status;
            }
            status = remove(mEnemies);
            if (status != Status::Success)
            {
                return status;
            }
            status = remove(mExits);
            if (status != Status::Success)
            {
                return status;
            }
            status = mRegistry.TryRelease(entity);
            if (status != Status::Success)
            {
                return status;
            }
        }
    }
    mRegistry.SetStructuralPhase(false);
    mCommandCount = 0;
    return Status::Success;
}
Status GameWorld::RunTick(TickInput input) noexcept
{
    LUDUS_PROFILE_SCOPE(WorldTick);
    if (!IsBuilt() || mPhase != Phase::Idle)
    {
        return Status::InvalidPhase;
    }
    if (!Finite(input.Axis))
    {
        mPhase = Phase::Faulted;
        return Status::InvalidConfiguration;
    }
    if (mTick == std::numeric_limits<uint64>::max())
    {
        mPhase = Phase::Faulted;
        return Status::IdentityExhausted;
    }
    auto& trace = mTrace[mTraceNext];
    trace = { .Tick = mTick + 1, .Input = input, .Completions = mCompletionCount };
    mTraceNext = (mTraceNext + 1) % 64;
    mTraceCount = std::min(mTraceCount + 1, static_cast<usize>(64));
    mPhase = Phase::BeginTick;
    mStagedCount = 0;
    mOutcomeCount = 0;
    for (auto& motion : mMotion.GetValues())
    {
        motion.Override = false;
    }
    const auto completionCount = mCompletionCount;
    mCompletionCount = 0;
    std::sort(mCompletions, mCompletions + completionCount, [](const Completion& first, const Completion& second) {
        return first.Request < second.Request;
    });
    for (usize index = 0; index < completionCount; ++index)
    {
        const auto& result = mCompletions[index];
        for (auto& lease : mRequests)
        {
            if (lease.Id == result.Request)
            {
                lease = {};
                break;
            }
        }
        auto* motion = mMotion.Find(result.Entity);
        if (!mRegistry.IsActive(result.Entity) || motion == nullptr || result.Request == 0 ||
            motion->PendingRequest != result.Request || motion->Revision != result.Revision)
        {
            ++mStaleCompletions;
            continue;
        }
        motion->PendingRequest = 0;
        if (result.State == CompletionState::Ready)
        {
            motion->Velocity = result.Velocity;
            motion->Override = true;
        }
    }
    for (auto& transform : mTransforms.GetValues())
    {
        transform.Previous = transform.Current;
    }
    mPhase = Phase::Intent;
    Intent(input);
    mPhase = Phase::Motion;
    MoveAndCollide();
    mPhase = Phase::Gameplay;
    // Required session outcomes are owned tick-local values until commit succeeds.
    Name nextLevel = mNextLevel;
    auto status = Gameplay(input, nextLevel);
    trace.Commands = mCommandCount;
    if (status == Status::Success)
    {
        mPhase = Phase::Commit;
        status = Commit();
    }
    trace.LastPhase = mPhase;
    trace.Result = status;
    trace.Contacts = mContactCount;
    trace.Presentation = mStagedCount;
    if (status != Status::Success)
    {
        mStagedCount = 0;
        mRegistry.SetStructuralPhase(false);
        mPhase = Phase::Faulted;
        return status;
    }
    mNextLevel = nextLevel;
    // Optional cosmetics may be dropped, required level outcome lives in state.
    for (usize index = 0; index < mStagedCount; ++index)
    {
        if (mOutboxCount == 64)
        {
            ++mDroppedPresentation;
        }
        else
        {
            mOutbox[mOutboxCount++] = mStaged[index];
        }
    }
    ++mTick;
    mPhase = Phase::Idle;
    return Status::Success;
}
bool GameWorld::Extract(float32 alpha, RenderFrame& frame) const noexcept
{
    LUDUS_PROFILE_SCOPE(WorldExtract);
    if (!IsBuilt() || mPhase != Phase::Idle || !std::isfinite(alpha) || alpha < 0 || alpha > 1)
    {
        return false;
    }
    RenderFrame candidate
    {
        .Camera = mLevel.Camera,
        .VerticalExtent = mLevel.VerticalExtent,
        .MinimumWidth = mLevel.Maximum.X - mLevel.Minimum.X,
        .Tick = mTick,
    };
    const auto add = [&](Draw draw) {
        if (candidate.Count == 64)
        {
            return false;
        }
        candidate.Draws[candidate.Count++] = draw;
        return true;
    };
    for (usize index = 0; index < mLevel.CollisionCount; ++index)
    {
        if (!add(
        {
            .Position = mLevel.Collision[index].Center,
            .HalfExtent = mLevel.Collision[index].HalfExtent,
            .Color = {0.2F, 0.25F, 0.35F, 1},
        }))
        {
            return false;
        }
    }
    EntityId entities[64] = {};
    usize count = 0;
    for (auto entity : mVisuals.GetOwners())
    {
        if (mRegistry.IsActive(entity))
        {
            entities[count++] = entity;
        }
    }
    std::sort(entities, entities + count, [&](EntityId first, EntityId second) {
        return mRegistry.GetCreationSequence(first) < mRegistry.GetCreationSequence(second);
    });
    for (usize index = 0; index < count; ++index)
    {
        const auto* transform = mTransforms.Find(entities[index]);
        const auto* visual = mVisuals.Find(entities[index]);
        const Vec2 position{transform->Previous.X + (transform->Current.X - transform->Previous.X) * alpha,
                            transform->Previous.Y + (transform->Current.Y - transform->Previous.Y) * alpha};
        Draw draw{ .Position = position, .HalfExtent = visual->HalfExtent, .Rotation = transform->Rotation };
        for (usize channel = 0; channel < 4; ++channel)
        {
            draw.Color[channel] = visual->Color[channel];
        }
        if (!add(draw))
        {
            return false;
        }
    }
    frame = candidate;
    return true;
}
uint64 GameWorld::GetReplayHash() const noexcept
{
    // Hash only a completed boundary; queued work belongs to the next tick.
    if (mPhase != Phase::Idle || mCommandCount != 0 || mCompletionCount != 0)
    {
        return 0;
    }
    uint64 hash = 14695981039346656037ULL;
    const auto mix = [&](uint64 value) {
        for (usize byte = 0; byte < 8; ++byte)
        {
            hash ^= (value >> (byte * 8)) & 255;
            hash *= 1099511628211ULL;
        }
    };
    const auto number = [&](float32 value) { mix(std::bit_cast<uint32>(value)); };
    mix(mTick);
    mix(mLevel.Seed);
    mix(mNextRequest);
    mix(mRegistry.GetCapacity());
    mix(mRegistry.GetAvailable());
    RequestLease requests[16] = {};
    usize requestCount = 0;
    for (const auto& request : mRequests)
    {
        if (request.Id != 0)
        {
            requests[requestCount++] = request;
        }
    }
    std::sort(requests, requests + requestCount, [](const RequestLease& first, const RequestLease& second) {
        return first.Id < second.Id;
    });
    mix(requestCount);
    for (usize index = 0; index < requestCount; ++index)
    {
        const auto& request = requests[index];
        mix(request.Id);
        mix(request.Revision);
        mix(request.CreationSequence);
    }
    const auto name = [&](const Name& value) {
        mix(value.View().size());
        for (const char ch : value.View())
        {
            mix(static_cast<uint8>(ch));
        }
    };
    const auto pair = [&](Vec2 value) {
        number(value.X);
        number(value.Y);
    };
    name(mLevel.Id);
    pair(mLevel.Minimum);
    pair(mLevel.Maximum);
    pair(mLevel.Camera);
    number(mLevel.VerticalExtent);
    mix(mLevel.CollisionCount);
    for (usize index = 0; index < mLevel.CollisionCount; ++index)
    {
        const auto& box = mLevel.Collision[index];
        name(box.Id);
        pair(box.Center);
        pair(box.HalfExtent);
    }
    mix(mLevel.EntityCount);
    for (usize index = 0; index < mLevel.EntityCount; ++index)
    {
        const auto& recipe = mLevel.Entities[index];
        name(recipe.Id);
        mix(static_cast<uint64>(recipe.Type));
        pair(recipe.Position);
        number(recipe.Rotation);
        pair(recipe.Scale);
        number(recipe.Speed);
        mix(recipe.Health);
        name(recipe.Sprite);
        name(recipe.Target);
        pair(recipe.HalfExtent);
        name(recipe.NextLevel);
    }
    name(mNextLevel);
    EntityId entities[64] = {};
    usize count = 0;
    for (auto entity : mTransforms.GetOwners())
    {
        entities[count++] = entity;
    }
    std::sort(entities, entities + count, [&](EntityId first, EntityId second) {
        return mRegistry.GetCreationSequence(first) < mRegistry.GetCreationSequence(second);
    });
    mix(count);
    for (usize index = 0; index < count; ++index)
    {
        const auto entity = entities[index];
        mix(mRegistry.GetCreationSequence(entity));
        mix(mRegistry.IsActive(entity));
        const auto* transform = mTransforms.Find(entity);
        number(transform->Current.X);
        number(transform->Current.Y);
        number(transform->Rotation);
        number(transform->Scale.X);
        number(transform->Scale.Y);
        if (const auto* motion = mMotion.Find(entity))
        {
            mix(1);
            number(motion->Velocity.X);
            number(motion->Velocity.Y);
            number(motion->Speed);
            mix(motion->Revision);
            mix(motion->PendingRequest);
            mix(motion->Override);
        }
        else
        {
            mix(0);
        }
        if (const auto* collider = mColliders.Find(entity))
        {
            mix(1);
            pair(collider->HalfExtent);
        }
        else
        {
            mix(0);
        }
        if (const auto* health = mHealth.Find(entity))
        {
            mix(health->Current);
        }
        else
        {
            mix(0);
        }
        if (const auto* player = mPlayers.Find(entity))
        {
            mix(1);
            mix(player->AttackReady);
        }
        else
        {
            mix(0);
        }
        if (const auto* enemy = mEnemies.Find(entity))
        {
            mix(1);
            mix(mRegistry.GetCreationSequence(enemy->Target));
            mix(enemy->WakeTick);
            mix(static_cast<uint64>(enemy->Phase));
        }
        else
        {
            mix(0);
        }
        if (const auto* exit = mExits.Find(entity))
        {
            mix(1);
            mix(exit->Entered);
            mix(exit->Touching);
            for (char ch : exit->NextLevel.View())
            {
                mix(static_cast<uint8>(ch));
            }
        }
        else
        {
            mix(0);
        }
    }
    return hash;
}
Status TickDriver::Advance(GameWorld& world, float64 delta, TickInput input, bool visible) noexcept
{
    if (!std::isfinite(delta) || delta < 0)
    {
        return Status::InvalidConfiguration;
    }
    if (!visible || mMode != Mode::Playing)
    {
        mDebt = 0;
        mInput.Cancel();
        return Status::Success;
    }
    mInput.Sample(input);
    mDiscarded += std::max(0.0, delta - 0.25);
    mDebt += std::min(delta, 0.25);
    usize count = 0;
    while (mDebt + 1e-12 >= TickDuration && count < 4)
    {
        const auto status = world.RunTick(mInput.Consume());
        if (status != Status::Success)
        {
            SetMode(Mode::Faulted);
            return status;
        }
        mDebt = std::max(0.0, mDebt - TickDuration);
        ++count;
    }
    if (mDebt >= TickDuration)
    {
        const auto remainder = std::fmod(mDebt, TickDuration);
        mDiscarded += mDebt - remainder;
        mDebt = remainder;
    }
    return Status::Success;
}
Status TickDriver::Step(GameWorld& world) noexcept
{
    if (mMode != Mode::Paused)
    {
        return Status::InvalidPhase;
    }
    const auto status = world.RunTick({});
    if (status != Status::Success)
    {
        SetMode(Mode::Faulted);
    }
    return status;
}
void TickDriver::SetMode(Mode mode) noexcept
{
    mMode = mode;
    mDebt = 0;
    mInput.Cancel();
}
void TickDriver::Reset() noexcept
{
    mMode = Mode::Playing;
    mDebt = 0;
    mInput.Cancel();
}
LevelResult Session::RequestLoad(std::string_view source) noexcept
{
    if (mTransition == std::numeric_limits<uint64>::max())
    {
        return {LevelError::Capacity, 0};
    }
    ++mTransition;
    mCandidate = GameWorld{};
    const auto result = ReadLevel(source, mDefinition);
    mStage = result.Error == LevelError::None ? LoadStage::Preparing : LoadStage::Failed;
    return result;
}
Status Session::PollLoad() noexcept
{
    Status status = Status::Success;
    if (mStage == LoadStage::Preparing)
    {
        status = mCandidate.Prepare(mDefinition);
        if (status == Status::Success)
        {
            mStage = LoadStage::Building;
        }
    }
    else if (mStage == LoadStage::Building)
    {
        status = mCandidate.BuildNext();
        if (status == Status::Success && mCandidate.IsBuilt())
        {
            mStage = LoadStage::Ready;
        }
    }
    if (status != Status::Success)
    {
        mCandidate = GameWorld{};
        mStage = LoadStage::Failed;
    }
    return status;
}
bool Session::Activate() noexcept
{
    if (mStage != LoadStage::Ready)
    {
        return false;
    }
    mActive = Move(mCandidate);
    mStage = LoadStage::Empty;
    Driver.Reset();
    return true;
}
void Session::CancelLoad() noexcept
{
    mCandidate = GameWorld{};
    mStage = LoadStage::Cancelled;
}
} // namespace ludus::world_demo
