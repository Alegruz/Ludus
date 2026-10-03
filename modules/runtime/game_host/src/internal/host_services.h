#pragma once

// Private host service implementation (project-live-reload design 6).
//
// Provides the game_api::HostServices table a module calls back into:
// diagnostics routed through LUDUS_LOG_*, bounded host-owned allocation, and
// logical resource resolution. The service context carries the session so a
// cached pointer cannot outlive its generation; the host revokes it on retire.

#include <ludus/runtime/game_api/services.h>

#include <ludus/foundation/base/types.h>

namespace ludus::runtime::game_host
{
using ludus::foundation::uint64;
using ludus::foundation::usize;

// Owns the service table and the per-session allocation ledger. One instance
// lives for one generation; the host destroys it during retire so a stale
// cached HostContext* resolves to a revoked table.
class HostServiceProvider final
{
public:
    HostServiceProvider() noexcept;
    ~HostServiceProvider() noexcept;

    HostServiceProvider(const HostServiceProvider&) = delete;
    HostServiceProvider& operator=(const HostServiceProvider&) = delete;
    HostServiceProvider(HostServiceProvider&& other) noexcept;
    HostServiceProvider& operator=(HostServiceProvider&& other) noexcept;

    [[nodiscard]] const game_api::HostServices& Services() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return Context_ != nullptr;
    }
    void Retire() noexcept;
    void GateWork() noexcept;
    void Activate() noexcept;
    void PinUntilProcessExit() noexcept;
    void CopyResourcesFrom(const HostServiceProvider& source) noexcept;

    // Count of outstanding host allocations handed to the module. Retire must
    // see this at zero (the module frees everything it took) before the host
    // releases the loader reference (design 6/8).
    [[nodiscard]] uint64 OutstandingAllocations() const noexcept;
    [[nodiscard]] uint64 OutstandingWork() const noexcept;

    // Register a logical asset ID -> current resource generation handle mapping
    // the module can resolve. Bounded; used by the supported asset fixture.
    void SetResource(uint64 logicalAssetId, uint64 resourceHandle) noexcept;

    // Opaque per-session context passed over the ABI as game_api::HostContext*.
    // Defined in the implementation; public only so the service callbacks in
    // the .cpp can interpret the decoded pointer.
    struct Context;

private:
    Context* Context_ = nullptr;
};
} // namespace ludus::runtime::game_host
