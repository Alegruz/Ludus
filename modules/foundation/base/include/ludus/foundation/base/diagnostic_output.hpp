#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::foundation::diagnostics
{
enum class DeliveryStatus : uint8
{
    Unavailable,
    Delivered,
    Failed
};

// Healthy startup only, once, before threads can report failures. Duplicates a
// connected AF_UNIX/SOCK_DGRAM descriptor and retains it until process exit.
// No replacement/shutdown: published descriptor lifetime cannot race a failure.
[[nodiscard]] bool ConfigureEmergencySocket(int descriptor) noexcept;

// Assertion transport: bounded attempts, MSG_DONTWAIT | MSG_NOSIGNAL. Never falls
// back to stderr. No allocation, stdio, callbacks, assertions or waiting for an
// application lock. OS scheduling is not a real-time guarantee.
[[nodiscard]] DeliveryStatus TryWriteEmergencyBytes(const char* data, usize size) noexcept;

// Emergency bytes only, not a replacement for Logging. No allocation, stdio,
// assertions or application callbacks. Linux stderr fallback can BLOCK. Write
// attempts are capped; SIGPIPE cleanup retries EINTR to avoid killing the logging caller.
// Neither operation has a time bound. False means incomplete/failed delivery.
// Caller supplies readable memory. Neither durable storage nor signal safety
// is promised. For ordinary Logging only; assertions must use TryWrite instead.
[[nodiscard]] bool WriteEmergencyBytes(const char* data, usize size) noexcept;

// -- Control endpoint -------------------------------------------------------
//
// A separate, versioned channel from the engine to an external diagnostic
// helper. It is the plumbing for a *later* explicit ASSERT continue-once /
// terminate decision; this milestone establishes and versions the channel but
// wires no decision into the failure runtime (ASSERT stays terminal).
//
// The endpoint is a connected AF_UNIX/SOCK_SEQPACKET descriptor supplied by the
// helper at launch and configured once during healthy startup, exactly like the
// report socket: it is never opened, replaced, or torn down from a failure
// handler.
//
// The wire format is an EXPLICIT little-endian byte encoding, not a
// compiler-padded C++ struct sent raw. Every message is a fixed 16-byte header
// followed by `Length` payload bytes:
//
//     offset 0  uint32  magic           CONTROL_PROTOCOL_MAGIC
//     offset 4  uint16  version         CONTROL_PROTOCOL_VERSION
//     offset 6  uint16  kind            ControlMessageType
//     offset 8  uint32  incidentId      0 for Hello/HelloAck; the active
//                                       incident for Decision* (later milestone)
//     offset 12 uint32  length          payload byte count that follows
//     offset 16 uint8[length]           payload (e.g. bounded owned report bytes
//                                       for a decision request; empty otherwise)
//
// Encode/decode go through the helpers below so the byte layout is the single
// source of truth on both sides. Lengths, versions, ids and kinds are validated;
// an unknown/stale/duplicate/malformed frame never authorizes continuation.

// Protocol identity. Bump the version on any wire-layout change so the engine
// and helper refuse a mismatch instead of misreading frames. This IPC version is
// independent of the terminal fatal-packet version.
inline constexpr uint32 CONTROL_PROTOCOL_MAGIC = 0x4C554443u; // "LUDC"
inline constexpr uint16 CONTROL_PROTOCOL_VERSION = 1u;
inline constexpr usize CONTROL_HEADER_SIZE = 16u;
inline constexpr usize CONTROL_MAX_PAYLOAD = 2048u; // matches the report bound

enum class ControlMessageType : uint16
{
    Hello = 1,    // engine -> helper: announce protocol; empty payload
    HelloAck = 2, // helper -> engine: accept the agreed version; empty payload
    // Reserved for the later ASSERT decision milestone; the byte layout is fixed
    // now so both sides agree, but this milestone's runtime never sends them:
    DecisionRequest = 3, // engine -> helper: incidentId + owned report payload
    DecisionReply = 4    // helper -> engine: incidentId + 1-byte ControlDecision
};

enum class ControlDecision : uint8
{
    Terminate = 0, // default / safe outcome; also used when the channel fails
    ContinueOnce = 1
};

// Decoded header view. The payload is referenced separately, never owned here.
struct ControlHeader
{
    uint16 Kind = 0; // ControlMessageType
    uint32 IncidentId = 0;
    uint32 Length = 0; // payload bytes following the header
};

// Write a validated 16-byte header into `buffer` (>= CONTROL_HEADER_SIZE) using
// the explicit little-endian layout above. Returns the header size, or 0 if the
// buffer is too small or the payload length exceeds CONTROL_MAX_PAYLOAD. No
// allocation; safe from any context.
[[nodiscard]] usize
EncodeControlHeader(char* buffer, usize capacity, ControlMessageType kind, uint32 incidentId, uint32 length) noexcept;

// Parse and validate a received frame's header from `buffer`/`size`. Rejects a
// short buffer, wrong magic/version, an unknown kind, or a payload length that
// would exceed the frame; returns false and leaves `header` unspecified.
[[nodiscard]] bool DecodeControlHeader(const char* buffer, usize size, ControlHeader& header) noexcept;

enum class ControlState : uint8
{
    Unconfigured, // no descriptor supplied
    Failed,       // descriptor supplied but invalid / handshake rejected
    Ready         // handshake completed; channel usable by a later milestone
};

// Healthy startup only, once, before any failure can occur. Duplicates the
// connected AF_UNIX/SOCK_SEQPACKET descriptor (CLOEXEC), performs the versioned
// Hello/HelloAck handshake (explicit byte encoding) with a bounded, nonblocking
// timeout, and retains the owned descriptor until process exit. Returns Ready
// only when the helper acknowledged the agreed version. A malformed/absent ack
// or a bad descriptor yields Failed; the engine still runs (report-only). No
// allocation, no stdio, no assertions. Not usable from a failure handler.
ControlState ConfigureControlEndpoint(int descriptor) noexcept;

// The last configured state. Cheap, lock-free; safe to read on the failure path
// by a later milestone before attempting a decision exchange.
[[nodiscard]] ControlState ControlEndpointState() noexcept;

// True when the process appears to run under continuous integration. Reads the
// environment (the generic CI marker plus common runner-specific variables, and
// an explicit LUDUS_CI=0/1 override), biasing toward true on ambiguity so a
// runner is never prompted. CI always forces report-only and, in a later
// milestone, vetoes an ASSERT continue. Query off the success path only.
[[nodiscard]] bool IsContinuousIntegration() noexcept;
} // namespace ludus::foundation::diagnostics
