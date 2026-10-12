#include <ludus/runtime/resources/manager.h>

#include "internal/counters.hpp"

#include <atomic>
#include <new>

namespace ludus::resources
{
namespace
{
constexpr uint32 NONE = 0xFFFFFFFFU;
std::atomic<uint64> NEXT_OWNER{1};
uint64 ReserveOwner() noexcept
{
    auto owner = NEXT_OWNER.load(std::memory_order_relaxed);
    while (owner != 0xFFFFFFFFFFFFFFFFULL)
    {
        if (NEXT_OWNER.compare_exchange_weak(owner, owner + 1, std::memory_order_relaxed))
        {
            return owner;
        }
    }
    return 0;
}
template <typename Record>
uint32 Free(Record* records, uint32 count) noexcept
{
    for (uint32 i = 0; i < count; ++i)
    {
        if (!records[i].Used && records[i].Generation != NONE)
        {
            return i;
        }
    }
    return NONE;
}
template <typename Record>
void Admit(Record& record) noexcept
{
    const auto generation = record.Generation;
    record = {};
    record.Generation = generation;
    (void)internal::Advance(record.Generation);
    record.Used = true;
}
} // namespace
// Thanks to Scott Bilas, “A Generic Handle-Based Resource Manager,” Game
// Programming Gems 1, §1.6 pp. 68–79, and James Boer, “Resource and Memory
// Management,” §1.7 pp. 80–87, for typed identity and pre-allocation reservations,
// as summarized in docs/architecture/resource-management-reference-review.md.
// Adaptations: independent requests/leases, nonwrapping release-build validation,
// separate domain reservations and acknowledged retirement; no book code copied.
// https://github.com/Alegruz/Ludus/blob/main/docs/architecture/resource-management.md
struct Manager::Impl final
{
    Config Limits;
    uint64 Owner = 0;
    bool Stopping = false;
    content::Resource Assets[64];
    uint32 AssetCount = 0;
    Adapter Adapters[3];
    uint32 AdapterCount = 0;
    struct Scope final
    {
        uint32 Generation = 0;
        bool Used = false;
        bool Closing = false;
        content::ResourceId Name;
    } Scopes[16];
    struct Request final
    {
        uint32 Generation = 0;
        bool Used = false;
        uint32 Scope = NONE;
        uint32 Operation = NONE;
        uint32 Version = NONE;
        RequestInfo Info;
    } Requests[32];
    struct Operation final
    {
        uint32 Generation = 0;
        bool Used = false;
        bool Started = false;
        bool Cleaning = false;
        uint32 Version = NONE;
    } Operations[8];
    struct Version final
    {
        uint32 Generation = 0;
        bool Used = false;
        bool Live = false;
        bool Retiring = false;
        bool PreparationCharged = true;
        uint32 Asset = NONE;
        uint32 AdapterIndex = NONE;
        uint64 Epoch = 0;
        void* Candidate = nullptr;
        const void* Payload = nullptr;
    } Versions[16];
    struct Hold final
    {
        uint32 Generation = 0;
        bool Used = false;
        uint32 Scope = NONE;
        uint32 Version = NONE;
    } Leases[64];
    uint64 Preparation = 0;
    uint64 Resident = 0;
    uint32 Cursor = 0;
    template <HandleKind K, typename Record>
    [[nodiscard]] bool Valid(Handle<K> handle, const Record* records, uint32 count) const noexcept
    {
        return handle.Owner == Owner && handle.Generation != 0 && handle.Slot < count && records[handle.Slot].Used &&
               records[handle.Slot].Generation == handle.Generation;
    }
    [[nodiscard]] uint32 FindAdapter(content::Kind kind) const noexcept
    {
        for (uint32 i = 0; i < AdapterCount; ++i)
        {
            if (Adapters[i].Type == kind)
            {
                return i;
            }
        }
        return NONE;
    }
    [[nodiscard]] bool Held(uint32 version) const noexcept
    {
        for (uint32 i = 0; i < Limits.Requests; ++i)
        {
            if (Requests[i].Used && Requests[i].Version == version)
            {
                return true;
            }
        }
        for (uint32 i = 0; i < Limits.Leases; ++i)
        {
            if (Leases[i].Used && Leases[i].Version == version)
            {
                return true;
            }
        }
        return false;
    }
    void RetireIfIdle(uint32 version) noexcept
    {
        if (version != NONE && Versions[version].Live && !Held(version))
        {
            Versions[version].Retiring = true;
        }
    }
    void CancelIfUnneeded(uint32 operation) noexcept
    {
        if (operation == NONE || Operations[operation].Cleaning)
        {
            return;
        }
        for (uint32 i = 0; i < Limits.Requests; ++i)
        {
            if (Requests[i].Used && Requests[i].Operation == operation)
            {
                return;
            }
        }
        auto& op = Operations[operation];
        op.Cleaning = true;
        auto& version = Versions[op.Version];
        if (version.Candidate != nullptr)
        {
            auto& adapter = Adapters[version.AdapterIndex];
            adapter.Cancel(adapter.Context, version.Candidate);
        }
    }
    void FreeVersion(uint32 index) noexcept
    {
        auto& version = Versions[index];
        const auto& cost = Adapters[version.AdapterIndex].Peak;
        if (version.PreparationCharged)
        {
            Preparation -= cost.PreparationBytes;
        }
        Resident -= cost.ResidentBytes;
        version.Used = false;
    }
    [[nodiscard]] Status Usable(const Version& version) const noexcept
    {
        const auto& adapter = Adapters[version.AdapterIndex];
        if (version.Epoch != adapter.Epoch(adapter.Context))
        {
            return Status::DeviceLost;
        }
        return adapter.Validate(adapter.Context, version.Payload);
    }
    void Complete(uint32 index, Status result) noexcept
    {
        auto& op = Operations[index];
        for (uint32 i = 0; i < Limits.Requests; ++i)
        {
            auto& request = Requests[i];
            if (request.Used && request.Operation == index)
            {
                request.Operation = NONE;
                request.Info = {result == Status::Ok ? RequestState::Ready : RequestState::Failed, result};
                if (result == Status::Ok)
                {
                    request.Version = op.Version;
                }
            }
        }
        auto& version = Versions[op.Version];
        if (result == Status::Ok)
        {
            version.Live = true;
            Preparation -= Adapters[version.AdapterIndex].Peak.PreparationBytes;
            version.PreparationCharged = false;
            op.Used = false;
            RetireIfIdle(op.Version);
        }
        else
        {
            op.Cleaning = true;
        }
    }
    void ServiceOperation(uint32 index) noexcept
    {
        auto& op = Operations[index];
        if (!op.Used)
        {
            return;
        }
        auto& version = Versions[op.Version];
        auto& adapter = Adapters[version.AdapterIndex];
        if (op.Cleaning)
        {
            if (version.Candidate == nullptr || adapter.Retire(adapter.Context, version.Candidate) == Status::Ok)
            {
                FreeVersion(op.Version);
                op.Used = false;
            }
            return;
        }
        if (adapter.Epoch(adapter.Context) != version.Epoch)
        {
            if (version.Candidate != nullptr)
            {
                adapter.Cancel(adapter.Context, version.Candidate);
            }
            Complete(index, Status::DeviceLost);
            return;
        }
        if (!op.Started)
        {
            const auto result = adapter.Begin(adapter.Context, Assets[version.Asset], version.Candidate);
            if (result == Status::QueueFull && version.Candidate == nullptr)
            {
                return;
            }
            if ((result != Status::Pending && result != Status::Ok) || version.Candidate == nullptr)
            {
                Complete(index, result == Status::Ok || result == Status::Pending ? Status::Failed : result);
                return;
            }
            op.Started = true;
            for (uint32 i = 0; i < Limits.Requests; ++i)
            {
                if (Requests[i].Used && Requests[i].Operation == index)
                {
                    Requests[i].Info.State = RequestState::Working;
                }
            }
        }
        const auto result = adapter.Poll(adapter.Context, version.Candidate, version.Payload);
        if (result == Status::Pending)
        {
            return;
        }
        if (result == Status::Ok)
        {
            Complete(index, version.Payload == nullptr ? Status::Failed : Usable(version));
        }
        else
        {
            Complete(index, result);
        }
    }
};
Manager::~Manager() noexcept
{
    LUDUS_REQUIRE(Shutdown() == Status::Ok);
    delete mImpl;
}
Status Manager::Initialize(const content::Catalog& catalog,
                           const Adapter* adapters,
                           usize count,
                           const Config& config) noexcept
{
    if (mImpl != nullptr && !mImpl->Stopping)
    {
        return Status::InvalidState;
    }
    if (mImpl != nullptr && (Inspect().Versions != 0 || Inspect().Requests != 0 || Inspect().Leases != 0))
    {
        return Status::InvalidState;
    }
    if (config.Assets == 0 || config.Assets > 64 || config.Scopes == 0 || config.Scopes > 16 || config.Requests == 0 ||
        config.Requests > 32 || config.Operations == 0 || config.Operations > 8 || config.Versions == 0 ||
        config.Versions > 16 || config.Leases == 0 || config.Leases > 64 || count > 3 ||
        (count != 0 && adapters == nullptr))
    {
        return Status::InvalidState;
    }
    if (catalog.Entries().size() > config.Assets)
    {
        return Status::QueueFull;
    }
    if (sizeof(Impl) > config.MetadataBytes)
    {
        return Status::BudgetExceeded;
    }
    for (usize i = 0; i < count; ++i)
    {
        const auto& adapter = adapters[i];
        if (adapter.Epoch == nullptr || adapter.Begin == nullptr || adapter.Poll == nullptr ||
            adapter.Cancel == nullptr || adapter.Retire == nullptr || adapter.Validate == nullptr ||
            adapter.Epoch(adapter.Context) == 0)
        {
            return Status::InvalidState;
        }
        for (usize j = 0; j < i; ++j)
        {
            if (adapter.Type == adapters[j].Type)
            {
                return Status::InvalidState;
            }
        }
    }
    auto* candidate = new (std::nothrow) Impl();
    if (candidate == nullptr)
    {
        return Status::OutOfMemory;
    }
    candidate->Owner = ReserveOwner();
    if (candidate->Owner == 0)
    {
        delete candidate;
        return Status::QueueFull;
    }
    candidate->Limits = config;
    for (const auto& entry : catalog.Entries())
    {
        candidate->Assets[candidate->AssetCount++] = entry;
    }
    candidate->AdapterCount = static_cast<uint32>(count);
    for (uint32 i = 0; i < candidate->AdapterCount; ++i)
    {
        candidate->Adapters[i] = adapters[i];
    }
    delete mImpl;
    mImpl = candidate;
    return Status::Ok;
}
Status Manager::Resolve(const content::ResourceId& id, content::Kind type, AssetHandle& output) const noexcept
{
    if (output.Owner != 0 || output.Slot != 0 || output.Generation != 0 || mImpl == nullptr || mImpl->Stopping)
    {
        return Status::InvalidState;
    }
    for (uint32 i = 0; i < mImpl->AssetCount; ++i)
    {
        const auto& asset = mImpl->Assets[i];
        if (asset.Type == type && asset.Id.View() == id.View())
        {
            output = {mImpl->Owner, i, 1};
            return Status::Ok;
        }
    }
    return Status::NotFound;
}
Status Manager::OpenScope(const content::ResourceId& name, ScopeHandle& output) noexcept
{
    if (output.Owner != 0 || output.Slot != 0 || output.Generation != 0 || mImpl == nullptr || mImpl->Stopping ||
        !content::ValidId(name.View()))
    {
        return Status::InvalidState;
    }
    const auto slot = Free(mImpl->Scopes, mImpl->Limits.Scopes);
    if (slot == NONE)
    {
        return Status::QueueFull;
    }
    auto& scope = mImpl->Scopes[slot];
    Admit(scope);
    scope.Name = name;
    output = {mImpl->Owner, slot, scope.Generation};
    return Status::Ok;
}
Status Manager::BeginAcquire(ScopeHandle scope, AssetHandle asset, RequestHandle& output) noexcept
{
    if (output.Owner != 0 || output.Slot != 0 || output.Generation != 0 || mImpl == nullptr || mImpl->Stopping)
    {
        return Status::InvalidState;
    }
    auto& state = *mImpl;
    if (!state.Valid(scope, state.Scopes, state.Limits.Scopes) || asset.Owner != state.Owner || asset.Generation != 1 ||
        asset.Slot >= state.AssetCount)
    {
        return Status::InvalidHandle;
    }
    if (state.Scopes[scope.Slot].Closing)
    {
        return Status::InvalidState;
    }
    const auto requestSlot = Free(state.Requests, state.Limits.Requests);
    if (requestSlot == NONE)
    {
        return Status::QueueFull;
    }
    const auto adapterIndex = state.FindAdapter(state.Assets[asset.Slot].Type);
    if (adapterIndex == NONE)
    {
        return Status::Unsupported;
    }
    const auto& adapter = state.Adapters[adapterIndex];
    const auto epoch = adapter.Epoch(adapter.Context);
    if (epoch == 0)
    {
        return Status::DeviceLost;
    }
    uint32 resident = NONE;
    uint32 operation = NONE;
    for (uint32 i = 0; i < state.Limits.Versions; ++i)
    {
        const auto& version = state.Versions[i];
        if (version.Used && version.Asset == asset.Slot && version.Epoch == epoch && version.Live &&
            !version.Retiring && state.Usable(version) == Status::Ok)
        {
            resident = i;
            break;
        }
    }
    if (resident == NONE)
    {
        for (uint32 i = 0; i < state.Limits.Operations; ++i)
        {
            const auto& op = state.Operations[i];
            if (op.Used && !op.Cleaning && state.Versions[op.Version].Asset == asset.Slot &&
                state.Versions[op.Version].Epoch == epoch)
            {
                operation = i;
                break;
            }
        }
        if (operation == NONE)
        {
            operation = Free(state.Operations, state.Limits.Operations);
            const auto versionSlot = Free(state.Versions, state.Limits.Versions);
            if (operation == NONE || versionSlot == NONE)
            {
                return Status::QueueFull;
            }
            if (adapter.Peak.PreparationBytes > state.Limits.PreparationBytes - state.Preparation ||
                adapter.Peak.ResidentBytes > state.Limits.ResidentBytes - state.Resident)
            {
                return Status::BudgetExceeded;
            }
            auto& op = state.Operations[operation];
            auto& version = state.Versions[versionSlot];
            Admit(op);
            Admit(version);
            op.Version = versionSlot;
            version.Asset = asset.Slot;
            version.AdapterIndex = adapterIndex;
            version.Epoch = epoch;
            state.Preparation += adapter.Peak.PreparationBytes;
            state.Resident += adapter.Peak.ResidentBytes;
        }
    }
    auto& request = state.Requests[requestSlot];
    Admit(request);
    request.Scope = scope.Slot;
    request.Operation = operation;
    request.Version = resident;
    if (resident != NONE)
    {
        request.Info = {RequestState::Ready, Status::Ok};
    }
    else if (state.Operations[operation].Started)
    {
        request.Info.State = RequestState::Working;
    }
    output = {state.Owner, requestSlot, request.Generation};
    return Status::Ok;
}
Status Manager::GetRequest(RequestHandle request, RequestInfo& output) const noexcept
{
    if (mImpl == nullptr || !mImpl->Valid(request, mImpl->Requests, mImpl->Limits.Requests))
    {
        return Status::InvalidHandle;
    }
    output = mImpl->Requests[request.Slot].Info;
    return Status::Ok;
}
Status Manager::TakeLease(RequestHandle request, Lease& output) noexcept
{
    if (output.mManager != nullptr)
    {
        return Status::InvalidState;
    }
    if (mImpl == nullptr || !mImpl->Valid(request, mImpl->Requests, mImpl->Limits.Requests))
    {
        return Status::InvalidHandle;
    }
    auto& record = mImpl->Requests[request.Slot];
    if (record.Info.State != RequestState::Ready)
    {
        return record.Info.Result;
    }
    const auto usable = mImpl->Usable(mImpl->Versions[record.Version]);
    if (usable != Status::Ok)
    {
        return usable;
    }
    const auto slot = Free(mImpl->Leases, mImpl->Limits.Leases);
    if (slot == NONE)
    {
        return Status::QueueFull;
    }
    auto& hold = mImpl->Leases[slot];
    Admit(hold);
    hold.Scope = record.Scope;
    hold.Version = record.Version;
    record.Used = false;
    output.mManager = this;
    output.mToken = {mImpl->Owner, slot, hold.Generation};
    return Status::Ok;
}
Status Manager::Cancel(RequestHandle request) noexcept
{
    if (mImpl == nullptr || !mImpl->Valid(request, mImpl->Requests, mImpl->Limits.Requests))
    {
        return Status::InvalidHandle;
    }
    auto& record = mImpl->Requests[request.Slot];
    const auto operation = record.Operation;
    const auto version = record.Version;
    record.Operation = NONE;
    record.Version = NONE;
    record.Info = {RequestState::Cancelled, Status::Cancelled};
    mImpl->CancelIfUnneeded(operation);
    mImpl->RetireIfIdle(version);
    return Status::Ok;
}
Status Manager::EndRequest(RequestHandle request) noexcept
{
    const auto result = Cancel(request);
    if (result == Status::Ok)
    {
        mImpl->Requests[request.Slot].Used = false;
    }
    return result;
}
Status Manager::Borrow(LeaseToken lease, content::Kind type, const void*& output) const noexcept
{
    if (mImpl == nullptr || !mImpl->Valid(lease, mImpl->Leases, mImpl->Limits.Leases))
    {
        return Status::InvalidHandle;
    }
    const auto& version = mImpl->Versions[mImpl->Leases[lease.Slot].Version];
    if (mImpl->Assets[version.Asset].Type != type)
    {
        return Status::InvalidHandle;
    }
    const auto result = mImpl->Usable(version);
    if (result == Status::Ok)
    {
        output = version.Payload;
    }
    return result;
}
Status Manager::Release(LeaseToken lease) noexcept
{
    if (mImpl == nullptr || !mImpl->Valid(lease, mImpl->Leases, mImpl->Limits.Leases))
    {
        return Status::InvalidHandle;
    }
    auto& hold = mImpl->Leases[lease.Slot];
    hold.Used = false;
    mImpl->RetireIfIdle(hold.Version);
    return Status::Ok;
}
Status Manager::GetScope(ScopeHandle scope, ScopeInfo& output) const noexcept
{
    if (mImpl == nullptr || !mImpl->Valid(scope, mImpl->Scopes, mImpl->Limits.Scopes))
    {
        return Status::InvalidHandle;
    }
    ScopeInfo info;
    info.Name = mImpl->Scopes[scope.Slot].Name;
    info.Closing = mImpl->Scopes[scope.Slot].Closing;
    for (uint32 i = 0; i < mImpl->Limits.Requests; ++i)
    {
        info.Requests += mImpl->Requests[i].Used && mImpl->Requests[i].Scope == scope.Slot ? 1U : 0U;
    }
    for (uint32 i = 0; i < mImpl->Limits.Leases; ++i)
    {
        info.Leases += mImpl->Leases[i].Used && mImpl->Leases[i].Scope == scope.Slot ? 1U : 0U;
    }
    output = info;
    return Status::Ok;
}
Status Manager::CloseScope(ScopeHandle scope) noexcept
{
    if (mImpl == nullptr || !mImpl->Valid(scope, mImpl->Scopes, mImpl->Limits.Scopes))
    {
        return Status::InvalidHandle;
    }
    mImpl->Scopes[scope.Slot].Closing = true;
    for (uint32 i = 0; i < mImpl->Limits.Requests; ++i)
    {
        if (mImpl->Requests[i].Used && mImpl->Requests[i].Scope == scope.Slot)
        {
            (void)EndRequest({mImpl->Owner, i, mImpl->Requests[i].Generation});
        }
    }
    for (uint32 i = 0; i < mImpl->Limits.Leases; ++i)
    {
        if (mImpl->Leases[i].Used && mImpl->Leases[i].Scope == scope.Slot)
        {
            return Status::Pending;
        }
    }
    mImpl->Scopes[scope.Slot].Used = false;
    return Status::Ok;
}
void Manager::Service(uint32 workItems) noexcept
{
    if (mImpl == nullptr)
    {
        return;
    }
    const auto total = mImpl->Limits.Operations + mImpl->Limits.Versions;
    if (workItems > total)
    {
        workItems = total;
    }
    for (uint32 i = 0; i < workItems; ++i)
    {
        const auto index = mImpl->Cursor;
        mImpl->Cursor = (index + 1) % total;
        if (index < mImpl->Limits.Operations)
        {
            mImpl->ServiceOperation(index);
        }
        else
        {
            const auto slot = index - mImpl->Limits.Operations;
            auto& version = mImpl->Versions[slot];
            if (version.Used && version.Retiring)
            {
                auto& adapter = mImpl->Adapters[version.AdapterIndex];
                if (adapter.Retire(adapter.Context, version.Candidate) == Status::Ok)
                {
                    mImpl->FreeVersion(slot);
                }
            }
        }
    }
}
Snapshot Manager::Inspect() const noexcept
{
    Snapshot result;
    if (mImpl == nullptr)
    {
        return result;
    }
    result.MetadataBytes = sizeof(Impl);
    result.PreparationBytes = mImpl->Preparation;
    result.ResidentBytes = mImpl->Resident;
    for (uint32 i = 0; i < mImpl->Limits.Requests; ++i)
    {
        result.Requests += mImpl->Requests[i].Used ? 1U : 0U;
    }
    for (uint32 i = 0; i < mImpl->Limits.Operations; ++i)
    {
        result.Operations += mImpl->Operations[i].Used ? 1U : 0U;
    }
    for (uint32 i = 0; i < mImpl->Limits.Versions; ++i)
    {
        result.Versions += mImpl->Versions[i].Used ? 1U : 0U;
    }
    for (uint32 i = 0; i < mImpl->Limits.Leases; ++i)
    {
        result.Leases += mImpl->Leases[i].Used ? 1U : 0U;
    }
    return result;
}
Status Manager::Shutdown() noexcept
{
    if (mImpl == nullptr)
    {
        return Status::Ok;
    }
    mImpl->Stopping = true;
    for (uint32 i = 0; i < mImpl->Limits.Scopes; ++i)
    {
        if (mImpl->Scopes[i].Used)
        {
            (void)CloseScope({mImpl->Owner, i, mImpl->Scopes[i].Generation});
        }
    }
    const auto snapshot = Inspect();
    return snapshot.Versions == 0 && snapshot.Leases == 0 && snapshot.Requests == 0 ? Status::Ok : Status::Pending;
}
Lease::~Lease() noexcept
{
    (void)Release();
}
Lease::Lease(Lease&& other) noexcept : mManager(other.mManager), mToken(other.mToken)
{
    other.mManager = nullptr;
    other.mToken = {};
}
Lease& Lease::operator=(Lease&& other) noexcept
{
    if (this != &other)
    {
        (void)Release();
        mManager = other.mManager;
        mToken = other.mToken;
        other.mManager = nullptr;
        other.mToken = {};
    }
    return *this;
}
LeaseToken Lease::Token() const noexcept
{
    return mToken;
}
Status Lease::Release() noexcept
{
    const auto result = mManager == nullptr ? Status::Ok : mManager->Release(mToken);
    mManager = nullptr;
    mToken = {};
    return result;
}
} // namespace ludus::resources
