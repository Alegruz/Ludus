#include "internal/diagnostic_platform.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
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

// Attempt a nonblocking connect to a Unix display socket without retaining it.
// Success proves a live endpoint is reachable, unlike a mere PATH/env lookup.
bool CanConnectUnix(const char* path) noexcept
{
    if (path == nullptr || path[0] == '\0')
    {
        return false;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const usize limit = sizeof(address.sun_path) - 1;
    usize length = 0;
    while (path[length] != '\0' && length < limit)
    {
        address.sun_path[length] = path[length];
        ++length;
    }
    if (path[length] != '\0')
    {
        return false; // Path too long for the address; treat as unavailable.
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
    {
        return false;
    }
    const int result = ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    // A nonblocking connect to a live listener returns 0 or EINPROGRESS/EISCONN.
    const bool reachable = result == 0 || errno == EINPROGRESS || errno == EISCONN;
    (void)::close(fd);
    return reachable;
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

bool HasInteractiveTerminal(int descriptor) noexcept
{
    const int saved_errno = errno;
    const bool interactive = ::isatty(descriptor) == 1;
    errno = saved_errno;
    return interactive;
}

bool HasGraphicalDisplay() noexcept
{
    const int saved_errno = errno;
    bool available = false;

    // Wayland: WAYLAND_DISPLAY names a socket under XDG_RUNTIME_DIR (or an
    // absolute path). Probe it rather than trusting the variable's presence.
    if (const char* wayland = ::getenv("WAYLAND_DISPLAY"); wayland != nullptr && wayland[0] != '\0')
    {
        if (wayland[0] == '/')
        {
            available = CanConnectUnix(wayland);
        }
        else if (const char* runtime = ::getenv("XDG_RUNTIME_DIR"); runtime != nullptr && runtime[0] != '\0')
        {
            char path[256];
            const usize runtime_length = ::strnlen(runtime, sizeof(path));
            const usize name_length = ::strnlen(wayland, sizeof(path));
            if (runtime_length + 1 + name_length < sizeof(path))
            {
                usize offset = 0;
                ::memcpy(path, runtime, runtime_length);
                offset += runtime_length;
                path[offset++] = '/';
                ::memcpy(path + offset, wayland, name_length);
                offset += name_length;
                path[offset] = '\0';
                available = CanConnectUnix(path);
            }
        }
    }

    // X11: DISPLAY like ":0" maps to the abstract/filesystem socket
    // /tmp/.X11-unix/X<n>. Only the local-socket form is validated here.
    if (!available)
    {
        if (const char* display = ::getenv("DISPLAY"); display != nullptr && display[0] == ':')
        {
            char path[64];
            usize offset = 0;
            const char prefix[] = "/tmp/.X11-unix/X";
            ::memcpy(path, prefix, sizeof(prefix) - 1);
            offset += sizeof(prefix) - 1;
            for (usize i = 1; display[i] != '\0' && display[i] != '.' && offset < sizeof(path) - 1; ++i)
            {
                path[offset++] = display[i];
            }
            path[offset] = '\0';
            available = CanConnectUnix(path);
        }
    }

    errno = saved_errno;
    return available;
}
} // namespace ludus::foundation::diagnostics::internal
