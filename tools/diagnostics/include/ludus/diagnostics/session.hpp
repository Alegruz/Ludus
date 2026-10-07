#pragma once

#include <ludus/foundation/base/types.h>

/// Startup-only development diagnostic integration.
///
/// This is a small integration layer that lives ABOVE FoundationBase (it depends
/// on Base; Base does not depend on it). It orchestrates the healthy-startup
/// configuration of the diagnostic transports and resolves interactive vs
/// report-only mode. It is NOT part of FoundationBase and adds nothing to any
/// engine public header or to the assertion hot path.
///
/// Call InitializeDiagnosticSession once, early in an application's main, BEFORE
/// engine workers and before logger setup. It reads the descriptors an external
/// diagnostic helper passed at launch, configures Base's report and control
/// transports, resolves the mode, and reports a failed interactive setup visibly.
/// It never launches a helper or any UI itself, and it never changes any
/// assertion's fatal action.

namespace ludus::diagnostics
{
/// Startup presentation intent, resolved against build flavor and the live environment.
enum class Interactivity : ludus::foundation::uint8
{
    Auto,      ///< Resolve build eligibility and live presentation at startup.
    ReportOnly ///< Disable control setup and interactive presentation.
};

/// Resolved presentation mode after startup inspected the environment.
enum class SessionMode : ludus::foundation::uint8
{
    ReportOnly, ///< Capture reports without an interactive session.
    Interactive ///< Eligible Debug, usable presentation and completed handshake.
};

/// Outcome of startup wiring. Informational; the process runs regardless.
struct SessionResult
{
    bool ReportTransportReady = false; ///< Owned report datagram socket configured.
    bool ControlEndpointReady = false; ///< Versioned control handshake completed.
    /// Resolved eligibility; does not change assertion fatal actions.
    SessionMode Mode = SessionMode::ReportOnly;
    bool DetectedCi = false;             ///< CI detected; forces report-only.
    bool InteractiveSetupFailed = false; ///< Requested interactive setup could not be completed.
};

/// Reads report/control descriptors from the environment, configures the
/// transports, resolves the mode, and (on eligible-but-failed interactive setup)
/// reports the failure visibly. Report-only in Development, CI, and headless
/// runs; interactive is eligible only in a local, non-CI Debug build with a live
/// terminal (Linux also supports a live Wayland/X11 display) AND a completed
/// control handshake. macOS Cocoa dialogs are deferred. CI is auto-detected and
/// always forces report-only. noexcept; performs no engine work; never launches a
/// helper/UI. Safe to call before the logger exists.
/// @pre Call once before workers, logging and any assertion failure.
/// @param requested Auto resolves eligibility; ReportOnly disables control setup.
/// @return Startup status; transport/presentation failures leave the process running.
/// @note Owned descriptors remain valid until process exit; no reconfiguration.
SessionResult InitializeDiagnosticSession(Interactivity requested = Interactivity::Auto) noexcept;
} // namespace ludus::diagnostics
