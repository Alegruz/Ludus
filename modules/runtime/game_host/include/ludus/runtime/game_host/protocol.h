#pragma once

// Versioned local play-session protocol (project-live-reload design 10).
//
// A persistent, bounded, asynchronous local channel between the editor's
// play-session supervisor and the GameHost. Control frames are 4-byte
// little-endian length + UTF-8 JSON on a private inherited socketpair, on
// descriptors separate from logs. This is NOT the old E0 frame format; it is a
// new versioned protocol. There is no listening network port or discovery.
//
// Every frame carries protocol version, ProjectEpoch, SessionId, RequestId and
// where applicable ModuleGeneration, SchemaEpoch and expected object revision.
// IDs are fixed-length hex to avoid float precision loss. The vocabulary and
// budgets below are the closed, versioned contract; unknown fields/versions are
// explicit errors.

#include <ludus/foundation/base/types.h>

#include <string_view>

namespace ludus::runtime::game_host::protocol
{
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::usize;

inline constexpr uint32 kProtocolVersion = 1;

// Bounds (design 10). Enforced by the framer and the command queue.
inline constexpr usize kMaxControlFrameBytes = 64u * 1024;    // 64 KiB control frame
inline constexpr usize kMaxSchemaTransferBytes = 256u * 1024; // 256 KiB schema transfer
inline constexpr uint32 kMaxPendingCommands = 128;            // 128 pending commands
inline constexpr usize kMaxCommandQueueBytes = 1024u * 1024;  // 1 MiB total command queue
inline constexpr uint32 kMaxPropertyBatch = 64;               // 64 property values per batch
inline constexpr uint32 kMaxRetainedResults = 128;            // mutation-result ledger
inline constexpr usize kMaxStringFieldBytes = 4096;           // 4 KiB string field

// Editor/supervisor -> host commands (design 10 minimum set).
enum class CommandKind : uint32
{
    Hello = 0,
    Status = 1,
    Load = 2,
    Pause = 3,
    Resume = 4,
    Step = 5,
    Reload = 6,
    ReadProperties = 7,
    ApplyEdits = 8,
    ReloadAsset = 9,
    Stop = 10
};

// Host -> editor/supervisor events (design 10).
enum class EventKind : uint32
{
    SessionReady = 0,
    ModuleReady = 1,
    CommandResult = 2,
    PropertiesChanged = 3,
    ReloadPhase = 4,
    SessionEnded = 5
};

// Terminal command status. Reuses the editor failure vocabulary plus the new
// explicit statuses required by the design. Ordinary returned failure,
// native HostFailed and CleanupUnknown stay distinguishable.
enum class CommandStatus : uint32
{
    Ok = 0,
    InvalidRequest = 1,
    Busy = 2,
    IncompatibleModule = 3,
    Superseded = 4,
    ReloadRejected = 5,
    RestartRequired = 6,
    StaleRevision = 7,
    SchemaChanged = 8,
    ProtocolError = 9,
    HostFailed = 10,
    CleanupUnknown = 11
};

// Reload phases reported in a ReloadPhase event (design 4/7).
enum class ReloadPhase : uint32
{
    Validate = 0,
    Quiesce = 1,
    Snapshot = 2,
    Stage = 3,
    Commit = 4,
    Retire = 5,
    Rejected = 6,
    Done = 7
};

[[nodiscard]] std::string_view CommandKindName(CommandKind kind) noexcept;
[[nodiscard]] std::string_view EventKindName(EventKind kind) noexcept;
[[nodiscard]] std::string_view CommandStatusName(CommandStatus status) noexcept;
[[nodiscard]] std::string_view ReloadPhaseName(ReloadPhase phase) noexcept;
} // namespace ludus::runtime::game_host::protocol
