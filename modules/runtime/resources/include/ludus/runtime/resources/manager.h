#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/content/content.h>

namespace ludus::resources
{
using namespace ludus::foundation;

/// Explicit admission, observation and cleanup outcomes; no exceptions.
enum class Status : uint8
{
    Ok,             ///< Operation completed or admission succeeded.
    Pending,        ///< Owned work or a close blocker remains; service and poll again.
    InvalidHandle,  ///< Wrong owner, slot, generation, type or consumed token.
    InvalidState,   ///< Nonempty output, closed admission or invalid configuration.
    QueueFull,      ///< A fixed table is full; failed admission owns nothing.
    BudgetExceeded, ///< Declared peak reservation would exceed a domain limit.
    OutOfMemory,    ///< Fixed control allocation failed.
    NotFound,       ///< Exact catalog identity is absent.
    Unsupported,    ///< No adapter supports the declared content kind.
    Cancelled,      ///< This caller cancelled; other callers are unaffected.
    DeviceLost,     ///< The adapter epoch changed; old ownership must still drain.
    Failed,         ///< Adapter validation, acquisition or preparation failed.
};
/// Compile-time identity categories; tokens of different categories cannot mix.
enum class HandleKind : uint8
{
    Asset,   ///< Immutable logical catalog entry.
    Scope,   ///< Tracked group of requests and externally owned leases.
    Request, ///< One caller's durable acquisition outcome.
    Lease,   ///< One hold on an exact resident version.
};
/// Nonowning 16-byte token. Copies never create ownership; zero is empty.
/// Validate on the issuing Manager's sole control owner before use.
template <HandleKind Kind>
struct Handle final
{
    uint64 Owner = 0;      ///< Nonreused process-local manager incarnation.
    uint32 Slot = 0;       ///< Fixed-table index; not a durable asset identifier.
    uint32 Generation = 0; ///< Nonwrapping slot incarnation; zero is invalid.
    /// Compare identity only, without establishing liveness.
    [[nodiscard]] bool operator==(const Handle&) const noexcept = default;
};
/// Identity without residency; Resolve never performs acquisition.
using AssetHandle = Handle<HandleKind::Asset>;
/// Scope identity; closed scopes remain alive while external leases block close.
using ScopeHandle = Handle<HandleKind::Scope>;
/// One caller's request, retained until TakeLease, EndRequest or CloseScope.
using RequestHandle = Handle<HandleKind::Request>;
/// One exact-version ownership hold; duplicate release is rejected.
using LeaseToken = Handle<HandleKind::Lease>;

/// Request state; terminal outcomes survive repeated observation and service.
enum class RequestState : uint8
{
    Queued,    ///< Accepted, awaiting adapter admission.
    Working,   ///< Shared operation owns candidate preparation.
    Ready,     ///< Complete payload held until claimed or cancelled.
    Failed,    ///< Terminal adapter failure; ownership cleanup may still be pending.
    Cancelled, ///< Caller demand dropped; shared cleanup is independent.
};
/// Bounded, copied request observation; no borrowed diagnostic storage.
struct RequestInfo final
{
    RequestState State = RequestState::Queued; ///< Current caller state.
    Status Result = Status::Pending;           ///< Terminal status or Pending.
};
/// Peak bytes declared by an adapter for one operation, charged once across waiters.
/// These are conservative reservations, not process or opaque driver measurements.
struct Cost final
{
    uint64 PreparationBytes = 0; ///< Input/scratch peak retained through failed cleanup.
    uint64 ResidentBytes = 0;    ///< Candidate/resident peak retained through physical retirement.
};
/// Explicit type adapter, copied at initialization. Context outlives the manager.
/// Functions run on its sole control owner, must not reenter it, and never wait
/// for worker/device completion. No client callbacks run inline during acquisition.
/// The context owns all candidate/payload storage and its durable worker results.
struct Adapter final
{
    content::Kind Type = content::Kind::Sound; ///< Exact supported catalog kind.
    void* Context = nullptr;                   ///< Application-owned composition context.
    Cost Peak; ///< Enforced reservation per admitted operation, including partial failures.
    uint64 (*Epoch)(void*) noexcept = nullptr; ///< Nonzero subsystem session; must not wrap/reuse.
    /// Begin must preserve a null candidate on QueueFull (retry) or rejection.
    /// Pending/Ok accepts owned candidate storage. A failed begin may return an
    /// owned candidate, which will be retired. Resources are borrowed for this call.
    Status (*Begin)(void*, const content::Resource&, void*& candidate) noexcept = nullptr;
    /// Observe durable completion. Ok supplies a stable nonnull immutable payload;
    /// all other results leave it null. Failed work can still own a candidate.
    Status (*Poll)(void*, void* candidate, const void*& payload) noexcept = nullptr;
    void (*Cancel)(void*, void* candidate) noexcept = nullptr; ///< Idempotent nonblocking cancellation flag.
    /// Idempotent begin/poll cleanup. Ok proves all candidate/payload allocations
    /// and submitted uses have physically drained; only then is storage reusable.
    /// Pending or an error retains ownership and budget for later service.
    Status (*Retire)(void*, void* candidate) noexcept = nullptr;
    Status (*Validate)(void*, const void* payload) noexcept = nullptr; ///< Ok only while payload remains usable.
};
/// Fixed capacities, bounded by the implementation maxima documented below.
/// Normal saturation never blocks cancellation, release, close or shutdown.
struct Config final
{
    uint32 Assets = 64;                        ///< At most 64 copied catalog entries (including source entries).
    uint32 Scopes = 16;                        ///< At most 16 tracked scopes.
    uint32 Requests = 32;                      ///< At most 32 durable request records.
    uint32 Operations = 8;                     ///< At most 8 shared operations.
    uint32 Versions = 16;                      ///< At most 16 candidates/residents/retiring versions.
    uint32 Leases = 64;                        ///< At most 64 external exact-version holds.
    uint64 MetadataBytes = uint64{256} * 1024; ///< Hard cap for fixed coordinator storage; allocator overhead excluded.
    uint64 PreparationBytes = uint64{128} * 1024 * 1024; ///< Hard cap for adapter-declared preparation reservations.
    uint64 ResidentBytes = uint64{256} * 1024 * 1024;    ///< Hard cap for adapter-declared resident reservations.
};
/// Immutable owner-side diagnostic copy; counts never include unacknowledged frees.
struct Snapshot final
{
    uint64 MetadataBytes = 0;    ///< Allocated coordinator storage (not allocator headers).
    uint64 PreparationBytes = 0; ///< Reserved peak for queued/working/failed cleanup.
    uint64 ResidentBytes = 0;    ///< Reserved candidate, active and retiring peak.
    uint32 Requests = 0;         ///< Accepted outcomes, including unconsumed terminal results.
    uint32 Operations = 0;       ///< Shared queued, working and cleaning operations.
    uint32 Versions = 0;         ///< Candidates, live residents and retiring residents.
    uint32 Leases = 0;           ///< External holds, including scope-close blockers.
};
/// Copied scope provenance and close blockers; all lease holds are externally owned.
struct ScopeInfo final
{
    content::ResourceId Name; ///< Purpose copied at OpenScope.
    bool Closing = false;     ///< New requests are refused once closing starts.
    uint32 Requests = 0;      ///< Tracked requests, including retained terminal outcomes.
    uint32 Leases = 0;        ///< External holds that block close until their owners release.
};
class Manager;
/// Move-only exact-version hold. Destroy/release on the manager's control owner.
/// @pre Manager outlives the wrapper and every Borrow interval.
class Lease final
{
public:
    /// Construct an empty output suitable for TakeLease.
    Lease() noexcept = default;
    /// Release a live hold without waiting or acquiring new work.
    ~Lease() noexcept;
    /// Copies do not create another hold.
    Lease(const Lease&) = delete;
    /// Copies do not create another hold.
    Lease& operator=(const Lease&) = delete;
    /// Transfer ownership and clear the source.
    Lease(Lease&& other) noexcept;
    /// Release this hold, then transfer and clear the source.
    Lease& operator=(Lease&& other) noexcept;
    /// Observe a token; copying it does not extend lifetime.
    [[nodiscard]] LeaseToken Token() const noexcept;
    /// Release and empty the wrapper; empty is Ok. Does not wait for retirement.
    [[nodiscard]] Status Release() noexcept;

private:
    friend class Manager;
    Manager* mManager = nullptr;
    LeaseToken mToken;
};
/// Fixed-capacity coordinator for one immutable catalog and one control owner.
/// All methods, lease destruction and borrowed-view use occur on that owner.
/// Workers operate only through adapter-owned immutable inputs and durable results.
/// No concurrent producer queue, reload, dependency DAG or streamed coverage yet.
class Manager final
{
public:
    /// Construct without allocation; Initialize performs fallible setup.
    Manager() noexcept = default;
    /// Reclaim fixed storage. @pre Shutdown has returned Ok; adapters outlive this call.
    ~Manager() noexcept;
    /// Coordinator identity and ownership cannot be copied.
    Manager(const Manager&) = delete;
    /// Coordinator identity and ownership cannot be assigned.
    Manager& operator=(const Manager&) = delete;
    /// Copy a catalog snapshot and up to three distinct adapter descriptors.
    /// Reinitialization is allowed only after Shutdown returns Ok, with a fresh owner.
    /// Failed setup preserves the previous drained incarnation and all outputs.
    /// Catalog files are not read. Adapter input snapshots/profiles must match this
    /// catalog and remain immutable; replacement requires a new manager incarnation.
    [[nodiscard]] Status Initialize(const content::Catalog& catalog,
                                    const Adapter* adapters,
                                    usize count,
                                    const Config& config = {}) noexcept;
    /// Exact ID/type lookup with no I/O or preparation. Output must be empty;
    /// failure preserves it. Logical handles survive payload retirement.
    [[nodiscard]] Status Resolve(const content::ResourceId& id, content::Kind type, AssetHandle& output) const noexcept;
    /// Copy a valid nonempty scope name; output must be empty. No resource work.
    [[nodiscard]] Status OpenScope(const content::ResourceId& name, ScopeHandle& output) noexcept;
    /// Reserve durable request, candidate/version and peak capacity before accepting.
    /// Equal asset/snapshot/epoch work shares an operation; a resident hit still
    /// returns a request. Empty output required; failure acquires no ownership.
    [[nodiscard]] Status BeginAcquire(ScopeHandle scope, AssetHandle asset, RequestHandle& output) noexcept;
    /// Nonblocking durable observation; output preserved for an invalid token.
    [[nodiscard]] Status GetRequest(RequestHandle request, RequestInfo& output) const noexcept;
    /// Transfer Ready ownership once into an empty move-only lease. Pending or
    /// failure preserves both outputs; lease-table saturation preserves Ready.
    [[nodiscard]] Status TakeLease(RequestHandle request, Lease& output) noexcept;
    /// Drop only this request's demand/ready hold; retain Cancelled until consumed.
    /// No worker/device wait; cancellation remains available under saturation.
    [[nodiscard]] Status Cancel(RequestHandle request) noexcept;
    /// Cancel if needed, discard durable outcome and invalidate the request.
    [[nodiscard]] Status EndRequest(RequestHandle request) noexcept;
    /// Borrow an immutable exact-version payload after type/epoch validation.
    /// Output preserved on failure; retain the lease and use only on control owner.
    [[nodiscard]] Status Borrow(LeaseToken lease, content::Kind type, const void*& output) const noexcept;
    /// Consume one live token; repeated/foreign release fails. Never waits.
    [[nodiscard]] Status Release(LeaseToken lease) noexcept;
    /// Copy bounded scope provenance and blockers; invalid handles preserve output.
    [[nodiscard]] Status GetScope(ScopeHandle scope, ScopeInfo& output) const noexcept;
    /// Close admission and end scope requests. External leases are never revoked;
    /// Pending until they are released. Ok invalidates the scope, which must then
    /// no longer be polled. Name/lease records identify blockers to the caller.
    [[nodiscard]] Status CloseScope(ScopeHandle scope) noexcept;
    /// Visit at most workItems operation/version records (default eight), using
    /// rotating bounded scans. Apply completion/publication and poll retirement.
    /// No allocation in the core; adapter cold stages can perform bounded work.
    void Service(uint32 workItems = 8) noexcept;
    /// Copy owner-side counters without allocation; an uninitialized manager is empty.
    [[nodiscard]] Snapshot Inspect() const noexcept;
    /// Permanently close admission and cancel/end requests. Pending while leases
    /// or adapter work/retirement remain. Release leases and Service, then poll;
    /// Ok permits destruction/reinitialization. Never joins or guesses quiescence.
    [[nodiscard]] Status Shutdown() noexcept;

private:
    struct Impl;
    Impl* mImpl = nullptr;
};
} // namespace ludus::resources
