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

// True when the process appears to run under continuous integration. Reads the
// environment; biases toward true on ambiguity so a runner is never prompted.
// This is the shared source of the CI decision: the startup integration uses it
// to force report-only, and the failure-path CI veto (later ASSERT-decision
// milestone) reuses the same primitive. Cheap, no allocation, no side effects.
bool DetectContinuousIntegration() noexcept;
} // namespace ludus::foundation::diagnostics::internal
