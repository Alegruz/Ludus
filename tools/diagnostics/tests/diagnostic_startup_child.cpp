// Startup/report-delivery test child. Fresh native process, production exception
// policy. It proves the report transport and the versioned control endpoint are
// wired at startup and that reports are visible/captured independently of normal
// Logging — before the logger exists and after it would have shut down.
//
// This child NEVER triggers an assertion's fatal action: this milestone does not
// change ASSERT. It uses only the startup integration and the report transport.

#include <ludus/diagnostics/session.hpp>
#include <ludus/foundation/base/diagnostic_output.hpp>

#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace fb = ludus::foundation;
namespace fbd = ludus::foundation::diagnostics;

#if defined(__cpp_exceptions)
#    error "Startup test child must use production exception policy"
#endif

namespace
{
void MarkStdout(const char* text)
{
    (void)::write(STDOUT_FILENO, text, std::strlen(text));
}

// Emit a report through the nonblocking assertion transport. This does not touch
// Logging: it is the same independent emergency byte path the assertion runtime
// uses, so a success here proves report visibility without a logger.
fbd::DeliveryStatus EmitReport(const char* label)
{
    char message[128];
    const int written = std::snprintf(message, sizeof(message), "[LUDUS report] %s\n", label);
    if (written <= 0)
    {
        return fbd::DeliveryStatus::Failed;
    }
    return fbd::TryWriteEmergencyBytes(message, static_cast<fb::usize>(written));
}
} // namespace

int main(int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "default";

    // Configure BEFORE any logger initialization would happen. In these tests
    // there is no logger at all, which is the point: reporting is independent.
    const ludus::diagnostics::SessionResult result = ludus::diagnostics::InitializeDiagnosticSession();

    char summary[256];
    const int summary_len = std::snprintf(summary,
                                          sizeof(summary),
                                          "STARTUP report=%d control=%d mode=%d ci=%d ifail=%d\n",
                                          static_cast<int>(result.ReportTransportReady),
                                          static_cast<int>(result.ControlEndpointReady),
                                          static_cast<int>(result.Mode),
                                          static_cast<int>(result.DetectedCi),
                                          static_cast<int>(result.InteractiveSetupFailed));
    if (summary_len > 0)
    {
        (void)::write(STDOUT_FILENO, summary, static_cast<fb::usize>(summary_len));
    }

    if (std::strcmp(mode, "pre-post") == 0)
    {
        const fbd::DeliveryStatus pre = EmitReport("pre-init");
        MarkStdout(pre == fbd::DeliveryStatus::Delivered     ? "PRE=delivered\n"
                   : pre == fbd::DeliveryStatus::Unavailable ? "PRE=unavailable\n"
                                                             : "PRE=failed\n");
        const fbd::DeliveryStatus post = EmitReport("post-shutdown");
        MarkStdout(post == fbd::DeliveryStatus::Delivered     ? "POST=delivered\n"
                   : post == fbd::DeliveryStatus::Unavailable ? "POST=unavailable\n"
                                                              : "POST=failed\n");
        return 0;
    }

    if (std::strcmp(mode, "stderr-closed") == 0)
    {
        // Close stderr, then report. The assertion transport is a datagram
        // socket, not stderr, so delivery must be unaffected by a closed stderr.
        (void)::close(STDERR_FILENO);
        const fbd::DeliveryStatus status = EmitReport("stderr-closed");
        MarkStdout(status == fbd::DeliveryStatus::Delivered ? "CLOSED=delivered\n" : "CLOSED=not-delivered\n");
        return 0; // Never crash on delivery outcome.
    }

    const fbd::DeliveryStatus status = EmitReport("default");
    MarkStdout(status == fbd::DeliveryStatus::Delivered     ? "DELIVERY=delivered\n"
               : status == fbd::DeliveryStatus::Unavailable ? "DELIVERY=unavailable\n"
                                                            : "DELIVERY=failed\n");
    return 0;
}
