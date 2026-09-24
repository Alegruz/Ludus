#include "internal/diagnostic_platform.hpp"

#include <ludus/foundation/base/diagnostic_output.hpp>

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace ludus::foundation::diagnostics::internal
{
uint64 NativeThreadId() noexcept
{
    return static_cast<uint64>(::syscall(SYS_gettid));
}

DebuggerState QueryDebugger() noexcept
{
    const int saved_errno = errno;
    const int fd = ::open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        errno = saved_errno;
        return DebuggerState::Unknown;
    }
    char buffer[4096];
    usize size = 0;
    for (uint32 attempt = 0; attempt < 4 && size < sizeof(buffer); ++attempt)
    {
        const auto count = ::read(fd, buffer + size, sizeof(buffer) - size);
        if (count > 0)
        {
            size += static_cast<usize>(count);
        }
        else if (count == 0 || errno != EINTR)
        {
            break;
        }
    }
    (void)::close(fd);
    errno = saved_errno;

    constexpr char key[] = "TracerPid:";
    for (usize pos = 0; pos < size; ++pos)
    {
        if (pos != 0 && buffer[pos - 1] != '\n')
        {
            continue;
        }
        usize matched = 0;
        while (matched < sizeof(key) - 1 && matched < size - pos && buffer[pos + matched] == key[matched])
        {
            ++matched;
        }
        if (matched != sizeof(key) - 1)
        {
            continue;
        }
        pos += matched;
        while (pos < size && (buffer[pos] == ' ' || buffer[pos] == '\t'))
        {
            ++pos;
        }
        bool digits = false;
        bool nonzero = false;
        while (pos < size && buffer[pos] >= '0' && buffer[pos] <= '9')
        {
            digits = true;
            nonzero = nonzero || buffer[pos] != '0';
            ++pos;
        }
        if (digits && pos < size && buffer[pos] == '\n')
        {
            return nonzero ? DebuggerState::Attached : DebuggerState::Detached;
        }
        return DebuggerState::Unknown;
    }
    return DebuggerState::Unknown;
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

namespace
{
bool NonEmptyEnv(const char* name) noexcept
{
    const char* value = ::getenv(name);
    return value != nullptr && value[0] != '\0';
}
} // namespace

bool DetectContinuousIntegration() noexcept
{
    const int saved_errno = errno;
    // Common CI signals. The generic CI variable covers GitHub Actions, GitLab,
    // CircleCI, Travis, and others; the rest catch environments that omit it.
    static const char* const kSignals[] =
        {"CI", "CONTINUOUS_INTEGRATION", "GITHUB_ACTIONS", "GITLAB_CI", "BUILDKITE", "JENKINS_URL", "TEAMCITY_VERSION"};
    bool detected = false;
    for (const char* signal : kSignals)
    {
        if (NonEmptyEnv(signal))
        {
            detected = true;
            break;
        }
    }
    // Explicit engine opt-out/opt-in wins over heuristics (LUDUS_CI=0/1).
    if (const char* forced = ::getenv("LUDUS_CI"); forced != nullptr && forced[0] != '\0')
    {
        detected = forced[0] != '0';
    }
    errno = saved_errno;
    return detected;
}
} // namespace ludus::foundation::diagnostics::internal

namespace ludus::foundation::diagnostics
{
bool IsContinuousIntegration() noexcept
{
    return internal::DetectContinuousIntegration();
}
} // namespace ludus::foundation::diagnostics
