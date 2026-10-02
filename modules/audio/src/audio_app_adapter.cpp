#include <ludus/audio/audio_app_adapter.h>

namespace ludus::audio::app
{
EventAdapter::EventAdapter(AudioSystem& system) noexcept : mSystem(system) {}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): policy table key.
uint32 EventAdapter::FindPolicy(uint32 eventId, uint32 ownerTag) const noexcept
{
    for (uint32 i = 0; i < POLICY_TABLE_CAPACITY; ++i)
    {
        if (mPolicy[i].InUse && mPolicy[i].EventId == eventId && mPolicy[i].OwnerTag == ownerTag)
        {
            return i;
        }
    }
    return POLICY_TABLE_CAPACITY;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): policy table key.
uint32 EventAdapter::AllocPolicy(uint32 eventId, uint32 ownerTag) noexcept
{
    for (uint32 i = 0; i < POLICY_TABLE_CAPACITY; ++i)
    {
        if (!mPolicy[i].InUse)
        {
            mPolicy[i] = PolicyEntry{};
            mPolicy[i].InUse = true;
            mPolicy[i].EventId = eventId;
            mPolicy[i].OwnerTag = ownerTag;
            return i;
        }
    }
    return POLICY_TABLE_CAPACITY;
}

bool EventAdapter::VoiceStillActive(VoiceHandle v) const noexcept
{
    if (!v.IsValid())
    {
        return false;
    }
    VoiceInfo info{};
    const Status s = mSystem.GetVoiceInfo(v, info);
    if (!IsOk(s))
    {
        // Reclaimed/stale generation of a previously accepted handle proves the
        // owner no longer owns a live slot (design section 4).
        return false;
    }
    return info.State != VoiceState::Terminal;
}

void EventAdapter::TrackLoop(uint32 ownerTag, VoiceHandle v) noexcept
{
    for (auto& loop : mLoops)
    {
        if (!loop.InUse)
        {
            loop.InUse = true;
            loop.OwnerTag = ownerTag;
            loop.Voice = v;
            return;
        }
    }
    // Table full: the loop still plays but is untracked for teardown. This is an
    // explicit bounded limit, surfaced via the sink.
    EmitOwner(OwnerEventKind::PolicySuppressed, Status::Ok, PolicyCause::TableFull, 0, ownerTag, 0);
}

void EventAdapter::EmitOwner(OwnerEventKind kind,
                             Status status,
                             PolicyCause cause,
                             uint32 tag, // NOLINT(bugprone-easily-swappable-parameters)
                             uint32 owner,
                             uint64 clock) noexcept
{
    if (mSink == nullptr)
    {
        return;
    }
    OwnerEvent ev{};
    ev.Kind = kind;
    ev.Result = status;
    ev.Policy = cause;
    ev.PolicyTag = tag;
    ev.OwnerKey = owner;
    ev.ApplicationClock = clock;
    mSink->OnOwnerEvent(ev);
}

TriggerResult EventAdapter::Trigger(const TriggerRequest& req, uint64 nowTicks) noexcept
{
    TriggerResult out{};

    // --- Policy evaluation BEFORE any reservation ---
    uint32 slot = FindPolicy(req.EventId, req.OwnerTag);
    if (slot < POLICY_TABLE_CAPACITY)
    {
        PolicyEntry& e = mPolicy[slot];
        // AlreadyActive: an accepted (incl. Pending/virtual) instance still live.
        if (req.SuppressWhileActive && e.HasActive && VoiceStillActive(e.Active))
        {
            out.Suppressed = SuppressReason::AlreadyActive;
            EmitOwner(OwnerEventKind::PolicySuppressed,
                      Status::Ok,
                      PolicyCause::AlreadyActive,
                      req.EventId,
                      req.OwnerTag,
                      nowTicks);
            return out;
        }
        // Cooldown: too soon since the last successful admission.
        if (e.CooldownTicks > 0 && nowTicks < e.LastAdmitTick + e.CooldownTicks)
        {
            out.Suppressed = SuppressReason::Cooldown;
            EmitOwner(OwnerEventKind::PolicySuppressed,
                      Status::Ok,
                      PolicyCause::Cooldown,
                      req.EventId,
                      req.OwnerTag,
                      nowTicks);
            return out;
        }
    }
    else
    {
        // New key: ensure there is table room before attempting admission.
        bool hasRoom = false;
        for (const auto& e : mPolicy)
        {
            if (!e.InUse)
            {
                hasRoom = true;
                break;
            }
        }
        if (!hasRoom)
        {
            out.Suppressed = SuppressReason::TableFull;
            EmitOwner(OwnerEventKind::PolicySuppressed,
                      Status::Ok,
                      PolicyCause::TableFull,
                      req.EventId,
                      req.OwnerTag,
                      nowTicks);
            return out;
        }
    }

    // --- Attempt admission ---
    VoiceHandle v{};
    const Status s = mSystem.PlayClip(req.Play, v);
    out.EngineStatus = s;
    if (!IsOk(s))
    {
        // Engine failure consumes no cooldown and leaves no active/duck state.
        EmitOwner(OwnerEventKind::VoiceCapacityRejected, s, PolicyCause::None, req.EventId, req.OwnerTag, nowTicks);
        return out;
    }

    // --- Only successful admission updates policy state ---
    if (slot >= POLICY_TABLE_CAPACITY)
    {
        slot = AllocPolicy(req.EventId, req.OwnerTag);
    }
    if (slot < POLICY_TABLE_CAPACITY)
    {
        PolicyEntry& e = mPolicy[slot];
        e.CooldownTicks = req.CooldownTicks;
        e.LastAdmitTick = nowTicks;
        e.HasActive = true;
        e.Active = v;
    }
    if (req.IsOwnedLoop)
    {
        TrackLoop(req.OwnerTag, v);
    }

    out.Admitted = true;
    out.Voice = v;
    return out;
}

void EventAdapter::Update(uint64 nowTicks) noexcept
{
    // Release active membership for durably-terminated voices, and reclaim
    // policy entries whose cooldown has elapsed and have no live instance.
    for (auto& e : mPolicy)
    {
        if (!e.InUse)
        {
            continue;
        }
        if (e.HasActive && !VoiceStillActive(e.Active))
        {
            e.HasActive = false;
            e.Active = VoiceHandle{};
        }
        const bool cooldownElapsed = e.CooldownTicks == 0 || nowTicks >= e.LastAdmitTick + e.CooldownTicks;
        if (!e.HasActive && cooldownElapsed)
        {
            e.InUse = false; // reclaim; owner-key reuse gets a fresh entry
        }
    }
    // Reap finished owned loops.
    for (auto& loop : mLoops)
    {
        if (loop.InUse && !VoiceStillActive(loop.Voice))
        {
            loop.InUse = false;
        }
    }
    // Release the shared dialogue duck after the last member terminates.
    if (mDuckActive)
    {
        bool any = false;
        for (auto& d : mDialogue)
        {
            if (d.InUse)
            {
                if (VoiceStillActive(d.Voice))
                {
                    any = true;
                }
                else
                {
                    d.InUse = false; // member durably terminated
                }
            }
        }
        if (!any)
        {
            // Remove the owned duck modifier (never restore bus gain to 1).
            (void)mSystem.ReleaseModifier(mDuck);
            mDuck = ModifierHandle{};
            mDuckActive = false;
        }
    }
}

void EventAdapter::StopOwnedLoops(uint32 ownerTag) noexcept
{
    for (auto& loop : mLoops)
    {
        if (loop.InUse && loop.OwnerTag == ownerTag)
        {
            (void)mSystem.Stop(loop.Voice);
            loop.InUse = false;
        }
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): named duck config.
void EventAdapter::ConfigureDialogueDuck(uint32 duckBusIndex, float32 duckDb) noexcept
{
    mDuckBus = duckBusIndex;
    mDuckDb = duckDb;
    mDuckConfigured = true;
}

TriggerResult EventAdapter::TriggerDialogue(const TriggerRequest& req, uint64 nowTicks) noexcept
{
    // Admit the dialogue line through the normal policed path first. A failed
    // admission must create no member and no duck.
    TriggerResult out = Trigger(req, nowTicks);
    if (!out.Admitted || !mDuckConfigured)
    {
        return out;
    }

    // On the first accepted member, acquire the shared duck modifier.
    if (!mDuckActive)
    {
        ModifierValue mv{};
        mv.BusIndex = mDuckBus;
        mv.TargetDb = mDuckDb;
        ModifierHandle h{};
        if (IsOk(mSystem.AcquireModifier(std::span<const ModifierValue>(&mv, 1), 1.0F, h)))
        {
            mDuck = h;
            mDuckActive = true;
        }
    }
    // Record membership.
    for (auto& d : mDialogue)
    {
        if (!d.InUse)
        {
            d.InUse = true;
            d.Voice = out.Voice;
            break;
        }
    }
    return out;
}

bool EventAdapter::DialogueDuckActive() const noexcept
{
    return mDuckActive;
}

uint32 EventAdapter::DialogueMemberCount() const noexcept
{
    uint32 n = 0;
    for (const auto& d : mDialogue)
    {
        if (d.InUse)
        {
            ++n;
        }
    }
    return n;
}

} // namespace ludus::audio::app
