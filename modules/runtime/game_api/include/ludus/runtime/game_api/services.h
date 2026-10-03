#pragma once

// Host service table (design section 6).
//
// The gameplay module calls a passed engine service table, never host-exported
// C++ symbols. Services cover explicit diagnostics, bounded allocation,
// frame/input data, resource IDs and the sample's render parameters. The table
// is grown with concrete consumer requirements, not by forwarding every engine
// API speculatively. All callbacks are noexcept POD function pointers.
//
// Service handles carry the session and resource generation; the host revokes a
// stale table after reload so a cached pointer cannot outlive its generation.
// Host allocations are released only through matching host services; a module
// never cross-module delete()s host memory (design 6).

#include <ludus/foundation/base/types.h>

namespace ludus::runtime::game_api
{
using ludus::foundation::float32;
using ludus::foundation::float64;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;
using ludus::foundation::usize;

// Diagnostic severity mirrored across the ABI as an explicit integer. The host
// maps these onto LUDUS_LOG_* so the module needs no logging headers. The
// 32-bit base is a fixed part of the ABI wire layout.
// NOLINTNEXTLINE(performance-enum-size)
enum class LogSeverity : uint32
{
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4
};

// Per-frame data handed to Update. Fixed-width, POD. Times are host-owned; the
// module does not advance wall-clock time itself (design 8). Input is the
// current held state, normalized by the host from the Platform contract.
struct FrameInput final
{
    uint64 FrameIndex = 0;
    float64 DeltaSeconds = 0;
    float64 ElapsedSeconds = 0; // Monotonic simulation seconds owned by host.
    uint32 Width = 0;
    uint32 Height = 0;
    // Bitset of currently-held logical actions (sample vocabulary). The host
    // maps bit 0 to Left (A/left arrow), bit 1 to Right (D/right arrow),
    // and bit 2 to Space. Focus/reset suppression follows Ludus::Input.
    uint32 HeldActions = 0;
    uint8 Paused = 0; // 1 while the host has the session paused.
    uint8 Reserved0 = 0;
    uint8 Reserved1 = 0;
    uint8 Reserved2 = 0;
};

// Render parameters the module writes each frame. The host owns the RHI session
// and performs the actual draw; the module only expresses a bounded, validated
// description (the supported reload fixture is a uniform-backed fullscreen
// effect, design 9/12). Colors are clamped by the host to [0,1].
struct RenderParams final
{
    float32 ClearRed = 0;
    float32 ClearGreen = 0;
    float32 ClearBlue = 0;
    float32 ClearAlpha = 1;
    // Four scalar uniform channels backing the supported shader effect. The
    // host copies these into the active uniform between frames.
    float32 Uniform0 = 0;
    float32 Uniform1 = 0;
    float32 Uniform2 = 0;
    float32 Uniform3 = 0;
};

// Opaque host context passed to every service call. Valid only for the lifetime
// of the generation that received it; the host revokes it on retire.
struct HostContext;

// The host service table. One pointer is handed to Create and stays valid for
// that instance's generation. Each entry is noexcept. A module must tolerate a
// null function pointer for optional services it did not request.
struct HostServices final
{
    uint32 StructSize = 0; // sizeof(HostServices) on the host; negotiated.
    uint32 Reserved = 0;
    HostContext* Context = nullptr;

    // Diagnostics: route a bounded UTF-8 message through host logging. The host
    // never lets a logging failure propagate into gameplay (AGENTS.md).
    void (*Log)(HostContext* ctx, LogSeverity severity, const char* text, usize length) noexcept = nullptr;

    // Bounded allocation owned by the host. Freed only via FreeBytes with the
    // same context. Returns nullptr on exhaustion; the module checks.
    void* (*AllocateBytes)(HostContext* ctx, usize size, usize alignment) noexcept = nullptr;
    void (*FreeBytes)(HostContext* ctx, void* ptr) noexcept = nullptr;

    // Resource identity: translate a logical asset ID (stable across reload)
    // into the host's current resource generation handle. Returns 0 if unknown.
    uint64 (*ResolveResource)(HostContext* ctx, uint64 logicalAssetId) noexcept = nullptr;
};
} // namespace ludus::runtime::game_api
