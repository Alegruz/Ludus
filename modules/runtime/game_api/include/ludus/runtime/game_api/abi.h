#pragma once

// Ludus gameplay module ABI (design section 6).
//
// This header defines the single boundary between a GameHost and a gameplay
// module. Everything here is plain data: fixed-width integers, explicit enum
// representations, byte/string views and opaque handles. No C++ classes with
// vtables, STL containers, std::function, exceptions or raw resource pointers
// cross this boundary. The authoring language stays C++23; a C-compatible binary
// layout does NOT promise arbitrary compiler/platform compatibility (design 6).
//
// A module exports exactly one unmangled entry point, LudusGetGameApi, which the
// host resolves after dlopen. It fills a caller-owned table of noexcept function
// pointers and returns an explicit status. Host-owned table storage survives the
// module's metadata lifetime; borrowed views last only for the call.

#include <ludus/foundation/base/types.h>

namespace ludus::runtime::game_api
{
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;

// ABI major version. Bumped on any incompatible layout/semantics change. The
// host rejects a module whose AbiMajor differs before calling Create.
inline constexpr uint32 kAbiMajor = 1;

// ABI minor version. Additive, backward-compatible table growth only. The host
// accepts a module whose AbiMinor <= host minor and negotiates the common size.
inline constexpr uint32 kAbiMinor = 0;

// Maximum byte length of an opaque identity string in module metadata. Keeps
// every metadata record a bounded POD; the host validates lengths before use.
inline constexpr uint32 kIdentityMax = 256;

// Explicit status for every boundary call. Ok is always success. The host never
// treats a non-zero status as a crash; native crashes/hangs are host failures
// handled by the supervisor, not catchable reload statuses (design 6/7).
enum class Status : uint32
{
    Ok = 0,
    InvalidArgument = 1,
    Unsupported = 2,
    IncompatibleAbi = 3,
    IncompatibleIdentity = 4,
    BufferTooSmall = 5,
    StaleRevision = 6,
    SchemaChanged = 7,
    OutOfRange = 8,
    NotPrepared = 9,
    MigrationUnsupported = 10,
    Internal = 11
};

// Optional capabilities a module advertises in its metadata. Mandatory
// operations (Query/Create/Destroy/Update) are always present; these flags gate
// the reload and live-edit tables. A module lacking Reload offers only Restart.
enum class Capability : uint32
{
    None = 0,
    Reload = 1u << 0,
    Properties = 1u << 1,
    AssetReload = 1u << 2
};

[[nodiscard]] constexpr Capability operator|(Capability a, Capability b) noexcept
{
    return static_cast<Capability>(static_cast<uint32>(a) | static_cast<uint32>(b));
}

[[nodiscard]] constexpr bool HasCapability(uint32 bits, Capability c) noexcept
{
    return (bits & static_cast<uint32>(c)) != 0;
}
} // namespace ludus::runtime::game_api
