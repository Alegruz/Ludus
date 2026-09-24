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

// Startup-only environment probes. These read the process environment and are
// used by the startup integration to pick interactive vs report-only mode; the
// CI probe is also the failure-path CI veto for a later ASSERT-decision
// milestone (cheap, no allocation, no side effects). Never launch a helper/UI.

// True when the process appears to run under continuous integration. Biases
// toward true on ambiguity so a runner is never prompted (spec R31/D2).
bool DetectContinuousIntegration() noexcept;

// True when a controlling terminal is present on the given descriptor and the
// process is not headless. Used to reject a prompt when I/O is unusable.
bool HasInteractiveTerminal(int descriptor) noexcept;

// Startup-only check that a usable graphical presentation channel exists. A
// found executable is NOT proof of working presentation: this validates a live
// display connection (Wayland/X11 env plus an openable endpoint), not merely
// that a dialog binary is on PATH. Returns false when headless.
bool HasGraphicalDisplay() noexcept;
} // namespace ludus::foundation::diagnostics::internal
