#pragma once

// Ludus::GameHost public surface (project-live-reload design 1/2).
//
// The GameHost is a Qt-free static runtime library that owns the engine
// lifecycles, the game window, the RHI session and ONE play session. It loads a
// project's native gameplay module (or, in a shipping build, dispatches to a
// statically linked implementation of the same game). The editor never links
// this; it supervises a host executable over the local play protocol.
//
// This header exposes only the host's run loop entry and configuration. The ABI
// identity, module loader, reload transaction and property plumbing are private
// implementation (src/internal). Public headers use Ludus aliases, explicit
// includes, no private/heavy headers and non-template boundaries (design 2).

#include <ludus/foundation/base/types.h>

#include <string_view>

namespace ludus::runtime::game_host
{
using ludus::foundation::int32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;

// How the host obtains the gameplay implementation. The same game source has
// one implementation; selecting static dispatch replaces only the module lookup
// (design 2). A shipping build links the module statically; a dev/editor build
// loads an immutable module generation by path.
// NOLINTNEXTLINE(performance-enum-size)
enum class GameplaySource : uint32
{
    // Load a native module generation from an absolute canonical path (dev).
    DynamicModule = 0,
    // Dispatch to the statically linked LudusGetGameApi in this executable.
    Static = 1
};

// How the host presents frames. A real windowed session drives the platform
// window + RHI; a headless session runs the full lifecycle without a surface so
// loader/reload/property behavior is testable where no GPU/display exists. A
// headless session is NOT proof of rendered-window behavior (requirement L15).
// NOLINTNEXTLINE(performance-enum-size)
enum class Presentation : uint32
{
    Windowed = 0,
    Headless = 1
};

// Terminal reason a host run loop ended. Distinct from any module-returned
// status: a crash/hang is handled by the supervisor, not reported here.
// NOLINTNEXTLINE(performance-enum-size)
enum class RunResult : int32
{
    Ok = 0,
    BadArguments = 1,
    ModuleLoadFailed = 2,
    IncompatibleModule = 3,
    WindowUnavailable = 4,
    RenderingUnavailable = 5,
    ProtocolError = 6,
    Internal = 7
};

// Static configuration for one host run. argv/cwd compatibility with the v1
// executable Run is preserved by the host executable's main (design 3).
struct HostConfig final
{
    GameplaySource Source = GameplaySource::DynamicModule;
    Presentation Mode = Presentation::Windowed;

    // Absolute canonical path inside a leased generation directory. Used only
    // for DynamicModule. Empty for Static.
    std::string_view ModulePath;

    // Protocol socket file descriptor inherited from the supervisor, or -1 to
    // run without a control channel (legacy Run / local smoke).
    int32 ControlFd = -1;

    // Project/game identity passed through to the module's Create.
    uint64 ProjectId = 0;
    uint64 GameId = 0;

    // Maximum frames to run before exiting (0 = run until Stop/no events).
    // A bounded run is used by headless acceptance so a CI run terminates.
    uint64 MaxFrames = 0;
};

// Run one host session to completion. Owns engine init/shutdown, window + RHI
// (for Windowed), the gameplay module lifecycle and, when a ControlFd is set,
// the play protocol loop. Never throws.
[[nodiscard]] RunResult Run(const HostConfig& config) noexcept;

// The host's own ABI identity string (the compatibility key it stamps and
// compares against a module's embedded identity). Exposed for the host
// executable to report in Hello and for acceptance tooling.
[[nodiscard]] std::string_view HostIdentity() noexcept;

// The host's gameplay ABI major/minor.
[[nodiscard]] uint32 HostAbiMajor() noexcept;
[[nodiscard]] uint32 HostAbiMinor() noexcept;
} // namespace ludus::runtime::game_host
