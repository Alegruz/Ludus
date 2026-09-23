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
} // namespace ludus::foundation::diagnostics
