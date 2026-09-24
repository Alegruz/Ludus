// Resumable-ASSERT decision test child. Fresh native process, production
// exception policy. It configures the report + control transports from inherited
// descriptors (as the startup integration would), then exercises the no-debugger
// dialog path: a failed enabled ASSERT sends a DecisionRequest and resumes only
// on an explicit ContinueOnce reply from the controlled test helper.
//
// There is no debugger here (native /proc TracerPid), so ResolveAssertDecision
// takes the control-endpoint branch. The driver scripts the helper's replies.

#include <ludus/foundation/base/assert.hpp>
#include <ludus/foundation/base/diagnostic_output.hpp>

#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace fb = ludus::foundation;
namespace fbd = ludus::foundation::diagnostics;

#if defined(__cpp_exceptions)
#    error "Decision test child must use production exception policy"
#endif

namespace
{
void Mark(const char* text)
{
    (void)::write(STDOUT_FILENO, text, std::strlen(text));
}

int gConditions = 0;

bool FailingCondition()
{
    ++gConditions;
    Mark("CONDITION\n");
    return false;
}

int ReadFd(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr ? std::atoi(value) : -1;
}
} // namespace

int main(int argc, char** argv)
{
    // Configure transports exactly like the startup integration would. Report is
    // optional here; the control endpoint is what drives the decision.
    const int report_fd = ReadFd("LUDUS_DIAGNOSTIC_REPORT_FD");
    if (report_fd >= 0 && !fbd::ConfigureEmergencySocket(report_fd))
    {
        return 79;
    }
    const int control_fd = ReadFd("LUDUS_DIAGNOSTIC_CONTROL_FD");
    if (control_fd >= 0 && fbd::ConfigureControlEndpoint(control_fd) != fbd::ControlState::Ready)
    {
        // A missing/failed handshake is a valid scenario the driver may assert on.
        Mark("CONTROL-NOT-READY\n");
    }

    const char* mode = argc > 1 ? argv[1] : "single";

    if (std::strcmp(mode, "single") == 0)
    {
        // One failing ASSERT. Resumes iff the helper replied ContinueOnce.
        LUDUS_ASSERT(FailingCondition(), "decision-probe");
        Mark("ASSERT-RETURNED\n");
        return gConditions == 1 ? 0 : 60;
    }
    if (std::strcmp(mode, "repeated") == 0)
    {
        // Each failing ASSERT is an independent incident: the slot is released
        // after a resume, so a later ASSERT is reportable again.
        LUDUS_ASSERT(FailingCondition(), "first");
        Mark("FIRST-RETURNED\n");
        LUDUS_ASSERT(FailingCondition(), "second");
        Mark("SECOND-RETURNED\n");
        return gConditions == 2 ? 0 : 61;
    }
    if (std::strcmp(mode, "require") == 0)
    {
        // REQUIRE must terminate even if the helper would say ContinueOnce.
        LUDUS_REQUIRE(FailingCondition(), "require-must-terminate");
        Mark("REQUIRE-RETURNED\n"); // Must never print.
        return 62;
    }

    Mark("UNKNOWN-MODE\n");
    return 63;
}
