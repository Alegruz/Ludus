#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::foundation
{
enum class StringStatus : uint8
{
    Ok,
    OutOfMemory,
    TooLarge,
    CapacityExceeded,
    InvalidArgument,
    InvalidUtf8,
    EmbeddedZero,
    InvalidHandle,
    WrongTable,
    NotFound,
    TokenExhausted,
    /// A table lookup visited every lookup slot without reaching the requested
    /// spelling or an empty slot. The load-factor invariant makes this unreachable
    /// for intact metadata, so it reports corrupted table state; the lookup leaves
    /// the table and the caller's output unchanged.
    ProbeExhausted,
};
} // namespace ludus::foundation
