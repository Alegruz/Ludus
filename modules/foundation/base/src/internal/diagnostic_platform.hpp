#pragma once

#include <ludus/foundation/base/compiler.hpp>
#include <ludus/foundation/base/types.h>

namespace ludus::foundation::diagnostics::internal
{
enum class DebuggerState : uint8
{
    Attached,
    Detached,
    Unknown
};

uint64 NativeThreadId() noexcept;
DebuggerState QueryDebugger() noexcept;
LUDUS_NOINLINE void BreakForDebugger() noexcept;
[[noreturn]] void TerminateForAssertion() noexcept;
[[noreturn]] void TerminateImmediately() noexcept;
} // namespace ludus::foundation::diagnostics::internal
