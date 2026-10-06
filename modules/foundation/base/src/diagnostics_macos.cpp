#include "internal/diagnostic_platform.hpp"

#include <cerrno>
#include <cstdlib>
#include <pthread.h>
#include <sys/sysctl.h>
#include <unistd.h>

// Thanks to Apple, Technical Q&A QA1361, "Detecting the Debugger", Listing 1:
// https://developer.apple.com/library/archive/qa/qa1361/_index.html
// Adapted sysctl/P_TRACED detection: failure is Unknown, never an assertion.
namespace ludus::foundation::diagnostics::internal
{
uint64 NativeThreadId() noexcept
{
    uint64 identifier = 0;
    (void)::pthread_threadid_np(nullptr, &identifier);
    return identifier;
}
DebuggerState QueryDebugger() noexcept
{
    const int savedErrno = errno;
    int query[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, ::getpid()};
    kinfo_proc process{};
    usize length = sizeof(process);
    const int result = ::sysctl(query, 4, &process, &length, nullptr, 0);
    errno = savedErrno;
    if (result != 0 || length != sizeof(process))
    {
        return DebuggerState::Unknown;
    }
    return (process.kp_proc.p_flag & P_TRACED) != 0 ? DebuggerState::Attached : DebuggerState::Detached;
}
void BreakForDebugger() noexcept
{
    __builtin_debugtrap();
}
[[noreturn]] void TerminateForAssertion() noexcept
{
    std::abort();
}
[[noreturn]] void TerminateImmediately() noexcept
{
    std::_Exit(134);
}
} // namespace ludus::foundation::diagnostics::internal
