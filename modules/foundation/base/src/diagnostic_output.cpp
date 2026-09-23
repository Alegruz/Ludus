#include <ludus/foundation/base/diagnostic_output.hpp>

#include <cerrno>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ludus::foundation::diagnostics
{
namespace
{
// Configuration is explicitly single-threaded before diagnostics are launched.
constinit int gEmergencySocket = -1;
} // namespace

bool ConfigureEmergencySocket(int descriptor) noexcept
{
    if (gEmergencySocket != -1)
    {
        return false;
    }
    const int saved_errno = errno;
    int type = 0;
    socklen_t length = sizeof(type);
    sockaddr_storage peer{};
    socklen_t peer_length = sizeof(peer);
    const bool valid = getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &type, &length) == 0 && type == SOCK_DGRAM &&
                       getpeername(descriptor, reinterpret_cast<sockaddr*>(&peer), &peer_length) == 0 &&
                       peer.ss_family == AF_UNIX;
    if (valid)
    {
        gEmergencySocket = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
    }
    errno = saved_errno;
    return gEmergencySocket != -1;
}

DeliveryStatus TryWriteEmergencyBytes(const char* data, usize size) noexcept
{
    if (gEmergencySocket == -1)
    {
        return DeliveryStatus::Unavailable;
    }
    if (data == nullptr || size > 2048)
    {
        return DeliveryStatus::Failed;
    }
    const int saved_errno = errno;
    DeliveryStatus status = DeliveryStatus::Failed;
    for (uint32 attempt = 0; attempt < 4; ++attempt)
    {
        const auto written = send(gEmergencySocket, data, size, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (written >= 0)
        {
            // Datagram writes are atomic: never split a record into new messages.
            status = static_cast<usize>(written) == size ? DeliveryStatus::Delivered : DeliveryStatus::Failed;
            break;
        }
        if (errno != EINTR)
        {
            break;
        }
    }
    errno = saved_errno;
    return status;
}

bool WriteEmergencyBytes(const char* data, usize size) noexcept
{
    const auto status = TryWriteEmergencyBytes(data, size);
    if (status != DeliveryStatus::Unavailable)
    {
        return status == DeliveryStatus::Delivered;
    }
    if (size == 0)
    {
        return true;
    }
    if (data == nullptr)
    {
        return false;
    }

    // Do not turn an ordinary log failure into SIGPIPE termination or install a
    // process-wide signal handler. Temporarily block SIGPIPE on this thread,
    // preserving pre-existing pending signals and the caller's mask/errno.
    const int saved_errno = errno;
    sigset_t blocked{};
    sigset_t previous{};
    sigset_t pending{};
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &blocked, &previous) != 0)
    {
        errno = saved_errno;
        return false;
    }
    bool delivered = false;
    if (sigpending(&pending) == 0)
    {
        const bool already_pending = sigismember(&pending, SIGPIPE) == 1;
        usize offset = 0;
        for (uint32 attempt = 0; attempt < 4 && offset < size; ++attempt)
        {
            const auto written = ::write(STDERR_FILENO, data + offset, size - offset);
            if (written > 0)
            {
                offset += static_cast<usize>(written);
            }
            else if (written < 0 && errno == EINTR)
            {
                continue;
            }
            else
            {
                if (written < 0 && errno == EPIPE && !already_pending)
                {
                    const timespec no_wait{};
                    // Drain our SIGPIPE before restoring the caller's mask. An
                    // EINTR retry cap here could deliver that signal and kill a
                    // logging caller. This safety cleanup can be delayed by
                    // a signal storm, another reason stderr is not time-bounded.
                    while (sigtimedwait(&blocked, nullptr, &no_wait) < 0 && errno == EINTR)
                    {
                    }
                }
                break;
            }
        }
        delivered = offset == size;
    }
    (void)pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    errno = saved_errno;
    return delivered;
}
} // namespace ludus::foundation::diagnostics
