#pragma once

#include <ludus/foundation/base/types.h>

// Startup-only diagnostic integration. Call InitializeDiagnostics exactly once,
// early in main, BEFORE spawning engine workers or initializing the logger, and
// before any code that could hit an assertion. It wires the report transport
// (and, when eligible, the versioned control endpoint) from descriptors an
// external diagnostic helper passed at launch.
//
// This header is deliberately tiny: it pulls in only the fixed-width aliases and
// declares plain aggregates and one function. It adds no STL, no OS headers, and
// nothing to the hot assertion path. Configuration happens here, at healthy
// startup; nothing in this API is ever called from a failure handler, and this
// API never launches a helper or any UI itself.

namespace ludus::foundation::diagnostics
{
// Interactive intent requested by the application/launcher, resolved against the
// build flavor and the live environment (CI, terminal, display).
enum class DiagnosticInteractivity : uint8
{
    // Let the runtime decide from build flavor + environment (the normal case).
    Auto,
    // Force report-only regardless of environment (Development, CI, headless,
    // or a local Debug run that opts out via LUDUS_DIAGNOSTIC_INTERACTIVE=0).
    ReportOnly
};

// Resolved presentation mode after InitializeDiagnostics has inspected the
// environment. Only Interactive is eligible to present a future ASSERT prompt;
// every other value is report-only for this and later milestones.
enum class DiagnosticMode : uint8
{
    ReportOnly, // report/capture only; no prompt will ever be shown
    Interactive // eligible Debug + non-CI + usable terminal or graphical display
};

// Outcome of startup wiring. All fields are informational; the process runs
// regardless. `InteractiveSetupFailed` is set when interactive mode was
// eligible and requested but graphical/terminal validation failed, so the
// caller can surface it (the runtime also reports it visibly to stderr).
struct DiagnosticStartupResult
{
    bool ReportTransportReady = false; // report datagram socket configured
    bool ControlEndpointReady = false; // versioned control handshake completed
    DiagnosticMode Mode = DiagnosticMode::ReportOnly;
    bool DetectedCi = false;             // CI environment detected (forces report-only)
    bool InteractiveSetupFailed = false; // eligible+requested interactive but no display/terminal
};

// Reads descriptors from the environment (report and control fds provided by
// the helper), configures the transports, and resolves the presentation mode.
//
// Rules encoded here:
//  * Interactive (graphical/prompt-eligible) mode is permitted ONLY in a local,
//    non-CI, resumable (Debug) build. Development and any CI run are report-only
//    and require no display or dialog helper.
//  * CI is auto-detected and always forces report-only, even for a locally built
//    Debug binary executed under CI.
//  * A local headless Debug run can opt into report-only via
//    DiagnosticInteractivity::ReportOnly or LUDUS_DIAGNOSTIC_INTERACTIVE=0.
//  * Graphical capability is validated by probing a live display connection at
//    startup, not by locating a dialog executable.
//  * Failed interactive setup is reported visibly and degrades to report-only;
//    it never aborts startup and never changes any assertion's fatal action.
//
// Safe to call before the logger exists. noexcept; performs no engine work.
DiagnosticStartupResult
InitializeDiagnostics(DiagnosticInteractivity requested = DiagnosticInteractivity::Auto) noexcept;
} // namespace ludus::foundation::diagnostics
