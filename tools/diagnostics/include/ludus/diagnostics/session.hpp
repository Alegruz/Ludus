#pragma once

#include <ludus/foundation/base/types.h>

// Startup-only development diagnostic integration.
//
// This is a small integration layer that lives ABOVE FoundationBase (it depends
// on Base; Base does not depend on it). It orchestrates the healthy-startup
// configuration of the diagnostic transports and resolves interactive vs
// report-only mode. It is NOT part of FoundationBase and adds nothing to any
// engine public header or to the assertion hot path.
//
// Call InitializeDiagnosticSession once, early in an application's main, BEFORE
// engine workers and before logger setup. It reads the descriptors an external
// diagnostic helper passed at launch, configures Base's report and control
// transports, resolves the mode, and reports a failed interactive setup visibly.
// It never launches a helper or any UI itself, and it never changes any
// assertion's fatal action.

namespace ludus::diagnostics
{
// Interactive intent, resolved against build flavor and the live environment.
enum class Interactivity : ludus::foundation::uint8
{
    Auto,      // decide from build flavor + environment (normal case)
    ReportOnly // force report-only (headless local Debug opt-out)
};

// Resolved presentation mode after startup inspected the environment.
enum class SessionMode : ludus::foundation::uint8
{
    ReportOnly, // capture/report only; no prompt will ever be shown
    Interactive // eligible Debug + non-CI + usable terminal or live display
};

// Outcome of startup wiring. Informational; the process runs regardless.
struct SessionResult
{
    bool ReportTransportReady = false; // report datagram socket configured
    bool ControlEndpointReady = false; // versioned control handshake completed
    SessionMode Mode = SessionMode::ReportOnly;
    bool DetectedCi = false;             // CI detected (forces report-only)
    bool InteractiveSetupFailed = false; // eligible+requested but no display/terminal/control
};

// Reads report/control descriptors from the environment, configures the
// transports, resolves the mode, and (on eligible-but-failed interactive setup)
// reports the failure visibly. Report-only in Development, CI, and headless
// runs; interactive is eligible only in a local, non-CI Debug build with a live
// terminal or display AND a completed control handshake. CI is auto-detected and
// always forces report-only. noexcept; performs no engine work; never launches a
// helper/UI. Safe to call before the logger exists.
SessionResult InitializeDiagnosticSession(Interactivity requested = Interactivity::Auto) noexcept;
} // namespace ludus::diagnostics
