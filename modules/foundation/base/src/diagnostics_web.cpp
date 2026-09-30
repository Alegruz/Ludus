#include "internal/diagnostic_platform.hpp"

#include <cstdlib>

namespace ludus::foundation::diagnostics::internal
{
uint64 NativeThreadId() noexcept
{
    // W1 uses one browser thread. This is a logical id, not a host OS tid.
    return 1;
}

DebuggerState QueryDebugger() noexcept
{
    // Browser DevTools attachment cannot be reliably detected. Never assume a
    // native debugger can continue past a WebAssembly trap.
    return DebuggerState::Unknown;
}

void BreakForDebugger() noexcept
{
    __builtin_trap();
}

[[noreturn]] void TerminateForAssertion() noexcept
{
    std::abort();
}

[[noreturn]] void TerminateImmediately() noexcept
{
    __builtin_trap();
}
} // namespace ludus::foundation::diagnostics::internal
