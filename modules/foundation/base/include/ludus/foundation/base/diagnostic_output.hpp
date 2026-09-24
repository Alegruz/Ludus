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
// handler. Message framing is fixed-size structs with a magic/version header;
// no allocation and no string parsing are involved.

// Protocol identity. Bump ProtocolVersion on any wire-layout change so the
// engine and helper can refuse a mismatch instead of misreading frames.
inline constexpr uint32 CONTROL_PROTOCOL_MAGIC = 0x4C554443u; // "LUDC"
inline constexpr uint16 CONTROL_PROTOCOL_VERSION = 1u;

enum class ControlMessageType : uint16
{
    Hello = 1,    // engine -> helper: announce magic/version/pid
    HelloAck = 2, // helper -> engine: accept and echo the agreed version
    // Reserved for the later ASSERT decision milestone; defined for versioning
    // only, not sent by this milestone's runtime:
    DecisionRequest = 3,
    DecisionReply = 4
};

enum class ControlDecision : uint8
{
    Terminate = 0, // default / safe outcome; also used when the channel fails
    ContinueOnce = 1
};

// Fixed-size, trivially-copyable frame. Same-toolchain SDK ABI only; never a
// persisted wire format. Fields are naturally aligned; no pointers cross it.
struct ControlFrame
{
    uint32 Magic = CONTROL_PROTOCOL_MAGIC;
    uint16 Version = CONTROL_PROTOCOL_VERSION;
    uint16 Type = 0;    // ControlMessageType
    uint32 Pid = 0;     // sender pid (Hello) / target pid (Decision*)
    uint32 Payload = 0; // ControlDecision (Decision*), else 0
};

enum class ControlState : uint8
{
    Unconfigured, // no descriptor supplied
    Failed,       // descriptor supplied but invalid / handshake rejected
    Ready         // handshake completed; channel usable by a later milestone
};

// Healthy startup only, once, before any failure can occur. Duplicates the
// connected AF_UNIX/SOCK_SEQPACKET descriptor (CLOEXEC), performs the versioned
// Hello/HelloAck handshake with a bounded, nonblocking timeout, and retains the
// owned descriptor until process exit. Returns Ready only when the helper
// acknowledged the agreed version. A malformed/absent ack or a bad descriptor
// yields Failed; the engine still runs (report-only). No allocation, no stdio,
// no assertions. Not usable from a failure handler.
ControlState ConfigureControlEndpoint(int descriptor) noexcept;

// The last configured state. Cheap, lock-free; safe to read on the failure path
// by a later milestone before attempting a decision exchange.
[[nodiscard]] ControlState ControlEndpointState() noexcept;
} // namespace ludus::foundation::diagnostics
