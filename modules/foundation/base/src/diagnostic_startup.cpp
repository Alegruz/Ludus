#include <ludus/foundation/base/diagnostic_startup.hpp>

#include <ludus/foundation/base/assert_config.hpp>
#include <ludus/foundation/base/diagnostic_output.hpp>

#include "internal/diagnostic_platform.hpp"

#include <cerrno>
#include <cstdlib>
#include <unistd.h>

namespace ludus::foundation::diagnostics
{
namespace
{
// Descriptors passed by the external diagnostic helper. The helper owns the
// collector ends; the engine only duplicates and reads what it is given.
constexpr const char* REPORT_FD_ENV = "LUDUS_DIAGNOSTIC_REPORT_FD";
constexpr const char* CONTROL_FD_ENV = "LUDUS_DIAGNOSTIC_CONTROL_FD";
// Local Debug opt-out. "0" forces report-only even on an interactive terminal.
constexpr const char* INTERACTIVE_ENV = "LUDUS_DIAGNOSTIC_INTERACTIVE";

// Parse a small nonnegative descriptor number without allocation or <charconv>.
// Returns -1 for absent/empty/malformed values so a bad env never picks a fd.
int ReadDescriptor(const char* name) noexcept
{
    const char* value = ::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return -1;
    }
    int result = 0;
    for (const char* cursor = value; *cursor != '\0'; ++cursor)
    {
        if (*cursor < '0' || *cursor > '9' || result > 100000000)
        {
            return -1;
        }
        result = result * 10 + (*cursor - '0');
    }
    return result;
}

// Best-effort visible notice for a failed interactive setup. Uses the ordinary
// blocking emergency writer (Logging's channel), never the assertion transport:
// this is a startup diagnostic, not a failure report, and must not consume the
// nonblocking assertion path. Failure to write is ignored.
void ReportVisible(const char* message) noexcept
{
    usize length = 0;
    while (message[length] != '\0')
    {
        ++length;
    }
    (void)WriteEmergencyBytes(message, length);
}
} // namespace

DiagnosticStartupResult InitializeDiagnostics(DiagnosticInteractivity requested) noexcept
{
    const int saved_errno = errno;
    DiagnosticStartupResult result{};

    // 1) Report transport: reuse the existing nonblocking datagram socket.
    const int report_fd = ReadDescriptor(REPORT_FD_ENV);
    if (report_fd >= 0)
    {
        result.ReportTransportReady = ConfigureEmergencySocket(report_fd);
    }

    // 2) Decide interactivity. Interactive is eligible only for a resumable
    //    (Debug) build. Development/Profile/Release are always report-only.
    result.DetectedCi = internal::DetectContinuousIntegration();

    // Interactive presentation is eligible only in the Debug flavor. This is
    // the same scope a later resumable-ASSERT policy value will carry; gating on
    // the existing flavor id keeps this startup milestone independent of that
    // not-yet-implemented policy and changes no assertion action here.
    const bool build_eligible = LUDUS_BUILD_FLAVOR_ID == 1; // Debug

    bool opt_out = requested == DiagnosticInteractivity::ReportOnly;
    if (const char* value = ::getenv(INTERACTIVE_ENV); value != nullptr && value[0] == '0')
    {
        opt_out = true; // Local headless Debug opts out explicitly.
    }

    const bool interactive_intended = build_eligible && !result.DetectedCi && !opt_out;

    // 3) Configure the control endpoint only when interactive is intended. In
    //    report-only builds/runs there is no display or dialog helper to talk
    //    to, so the control channel is left unconfigured.
    if (interactive_intended)
    {
        const int control_fd = ReadDescriptor(CONTROL_FD_ENV);
        if (control_fd >= 0)
        {
            result.ControlEndpointReady = ConfigureControlEndpoint(control_fd) == ControlState::Ready;
        }

        // 4) Validate real presentation capability. A found dialog executable is
        //    not proof; require a usable terminal or a live display connection.
        const bool terminal =
            internal::HasInteractiveTerminal(STDIN_FILENO) && internal::HasInteractiveTerminal(STDOUT_FILENO);
        const bool graphical = internal::HasGraphicalDisplay();
        const bool presentable = (terminal || graphical) && result.ControlEndpointReady;

        if (presentable)
        {
            result.Mode = DiagnosticMode::Interactive;
        }
        else
        {
            result.Mode = DiagnosticMode::ReportOnly;
            result.InteractiveSetupFailed = true;
            ReportVisible("[LUDUS diagnostics] interactive assertion setup unavailable "
                          "(no usable terminal/display or control endpoint); using report-only mode\n");
        }
    }
    else
    {
        result.Mode = DiagnosticMode::ReportOnly;
    }

    errno = saved_errno;
    return result;
}
} // namespace ludus::foundation::diagnostics
