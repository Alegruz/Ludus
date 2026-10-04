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
};
} // namespace ludus::foundation
