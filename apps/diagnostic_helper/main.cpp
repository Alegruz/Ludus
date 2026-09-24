// Ludus external diagnostic helper (Linux).
//
// A tiny, standalone launcher/collector that sits OUTSIDE the engine process. It
// owns the collector ends of the diagnostic channels, launches the engine binary
// with the engine-side descriptors, drains bounded report datagrams, answers the
// versioned control handshake, and cleans up when the child exits.
//
// It does not link any engine library. It reuses only the public protocol
// constants/frame layout from <ludus/foundation/base/diagnostic_output.hpp>
// (header-only; depends solely on the fixed-width type aliases) so the wire
// format has a single source of truth.
//
// Scope for this milestone: establish and version the channels and prove report
// delivery is independent of the engine's normal Logging. It does NOT decide
// ASSERT continuation; it acknowledges Hello and stands by. Graphical prompting
// is out of scope here and is gated by build flavor/environment in the engine.
//
// Usage:
//   ludus_diagnostic_helper [--report-only] [--] <engine-binary> [args...]
//
// The helper never changes any assertion's fatal action. It exists only to make
// reports visible/capturable and to hold the control channel open.

#include <ludus/foundation/base/diagnostic_output.hpp>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
using ludus::foundation::diagnostics::CONTROL_PROTOCOL_MAGIC;
using ludus::foundation::diagnostics::CONTROL_PROTOCOL_VERSION;
using ludus::foundation::diagnostics::ControlFrame;
using ludus::foundation::diagnostics::ControlMessageType;

constexpr int REPORT_BUFFER = 4096;

// Write a NUL-terminated string to a descriptor, tolerating short writes/EINTR.
void WriteAll(int fd, const char* text) noexcept
{
    ludus::foundation::usize length = std::strlen(text);
    ludus::foundation::usize offset = 0;
    while (offset < length)
    {
        const ssize_t written = ::write(fd, text + offset, length - offset);
        if (written > 0)
        {
            offset += static_cast<ludus::foundation::usize>(written);
        }
        else if (written < 0 && errno == EINTR)
        {
            continue;
        }
        else
        {
            return;
        }
    }
}

// Set an integer environment variable for the child (small, no allocation game).
void SetFdEnv(const char* name, int fd) noexcept
{
    char value[16];
    std::snprintf(value, sizeof(value), "%d", fd);
    ::setenv(name, value, 1);
}

// Answer exactly one Hello with a HelloAck echoing the agreed version. Returns
// true if a well-formed Hello was seen and acknowledged. Bounded and
// nonblocking-friendly: this runs once at startup, never in a failure handler.
bool AnswerControlHandshake(int control) noexcept
{
    ControlFrame incoming{};
    const ssize_t count = ::recv(control, &incoming, sizeof(incoming), 0);
    if (count != static_cast<ssize_t>(sizeof(incoming)))
    {
        return false;
    }
    if (incoming.Magic != CONTROL_PROTOCOL_MAGIC || incoming.Version != CONTROL_PROTOCOL_VERSION ||
        incoming.Type != static_cast<ludus::foundation::uint16>(ControlMessageType::Hello))
    {
        return false;
    }
    ControlFrame ack{};
    ack.Type = static_cast<ludus::foundation::uint16>(ControlMessageType::HelloAck);
    ack.Pid = incoming.Pid;
    ack.Payload = 0;
    ssize_t sent = -1;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        sent = ::send(control, &ack, sizeof(ack), MSG_NOSIGNAL);
        if (sent >= 0 || errno != EINTR)
        {
            break;
        }
    }
    return sent == static_cast<ssize_t>(sizeof(ack));
}
} // namespace

int main(int argc, char** argv)
{
    // Parse minimal options, then the engine command.
    int index = 1;
    for (; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--report-only") == 0)
        {
            // Report-only is the helper's only behavior this milestone; accepted
            // for forward-compatibility with a later interactive helper.
            continue;
        }
        if (std::strcmp(argv[index], "--") == 0)
        {
            ++index;
            break;
        }
        if (argv[index][0] != '-')
        {
            break;
        }
        WriteAll(STDERR_FILENO, "[helper] unknown option\n");
        return 2;
    }
    if (index >= argc)
    {
        WriteAll(STDERR_FILENO, "usage: ludus_diagnostic_helper [--report-only] [--] <engine-binary> [args...]\n");
        return 2;
    }

    // Report channel: connected AF_UNIX datagram pair. Engine sends bounded
    // report bytes; helper drains them. Control channel: SEQPACKET for framed
    // handshake messages.
    int report[2];
    int control[2];
    if (::socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, report) != 0 ||
        ::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, control) != 0)
    {
        WriteAll(STDERR_FILENO, "[helper] failed to create diagnostic channels\n");
        return 1;
    }
    // report[0]/control[0] stay with the helper (collector). report[1]/control[1]
    // go to the engine. Ignore SIGPIPE so a dead engine can never kill us.
    ::signal(SIGPIPE, SIG_IGN);

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        WriteAll(STDERR_FILENO, "[helper] fork failed\n");
        return 1;
    }
    if (pid == 0)
    {
        // Child: hand the engine ends to the engine via inheritable fds. Clear
        // CLOEXEC on the engine ends so they survive exec; the collector ends
        // are CLOEXEC and will not leak into the engine.
        ::fcntl(report[1], F_SETFD, 0);
        ::fcntl(control[1], F_SETFD, 0);
        SetFdEnv("LUDUS_DIAGNOSTIC_REPORT_FD", report[1]);
        SetFdEnv("LUDUS_DIAGNOSTIC_CONTROL_FD", control[1]);
        ::execvp(argv[index], &argv[index]);
        // exec failed.
        WriteAll(STDERR_FILENO, "[helper] failed to launch engine binary\n");
        ::_exit(127);
    }

    // Parent (collector). Close the engine ends we don't own.
    ::close(report[1]);
    ::close(control[1]);

    // Handshake once, up front. A missing/malformed handshake is not fatal to
    // the helper: report delivery still works, and the engine falls back to
    // report-only. We simply note it.
    (void)AnswerControlHandshake(control[0]);

    // Drain report datagrams until the child exits and the pipe is empty. Poll
    // both the report socket and the child status without blocking forever.
    bool child_done = false;
    int child_status = 0;
    while (true)
    {
        pollfd waiters[1];
        waiters[0].fd = report[0];
        waiters[0].events = POLLIN;
        const int ready = ::poll(waiters, 1, 100);
        if (ready > 0 && (waiters[0].revents & POLLIN) != 0)
        {
            char buffer[REPORT_BUFFER];
            const ssize_t got = ::recv(report[0], buffer, sizeof(buffer), MSG_DONTWAIT);
            if (got > 0)
            {
                // Surface captured reports to the helper's stdout so a launcher
                // or CI log retains them independently of engine Logging.
                ludus::foundation::usize offset = 0;
                while (offset < static_cast<ludus::foundation::usize>(got))
                {
                    const ssize_t written =
                        ::write(STDOUT_FILENO, buffer + offset, static_cast<ludus::foundation::usize>(got) - offset);
                    if (written > 0)
                    {
                        offset += static_cast<ludus::foundation::usize>(written);
                    }
                    else if (written < 0 && errno == EINTR)
                    {
                        continue;
                    }
                    else
                    {
                        break;
                    }
                }
                continue; // Drain any further queued datagrams promptly.
            }
        }
        if (!child_done)
        {
            const pid_t waited = ::waitpid(pid, &child_status, WNOHANG);
            if (waited == pid)
            {
                child_done = true;
                // Loop once more to drain any last datagrams already queued.
                continue;
            }
        }
        else if (ready == 0)
        {
            break; // Child gone and no more pending reports.
        }
    }

    // Cleanup: descriptors are the helper's; close them on child exit.
    ::close(report[0]);
    ::close(control[0]);

    if (WIFEXITED(child_status))
    {
        return WEXITSTATUS(child_status);
    }
    if (WIFSIGNALED(child_status))
    {
        // Mirror the child's terminating signal in our exit code (128+signal),
        // the shell convention, so a launcher sees the engine crashed.
        return 128 + WTERMSIG(child_status);
    }
    return 0;
}
