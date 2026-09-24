#include <ludus/foundation/base/diagnostic_output.hpp>

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
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
constinit int gControlSocket = -1;
constinit ControlState gControlState = ControlState::Unconfigured;

// Bounded startup handshake: never block engine launch indefinitely on a
// misbehaving helper. This is a startup wait, not a failure-path bound.
constexpr int CONTROL_HANDSHAKE_TIMEOUT_MS = 2000;
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

namespace
{
// Bounded, EINTR-aware receive of exactly one frame within the handshake
// deadline. Returns true only for a complete, well-formed frame of the expected
// type/version. Never blocks past the deadline; a slow/absent helper fails.
bool ReceiveControlFrame(int descriptor, ControlFrame& frame, ControlMessageType expected, int timeout_ms) noexcept
{
    pollfd waiter{};
    waiter.fd = descriptor;
    waiter.events = POLLIN;
    const int ready = ::poll(&waiter, 1, timeout_ms);
    if (ready <= 0 || (waiter.revents & POLLIN) == 0)
    {
        return false;
    }
    ControlFrame received{};
    // SEQPACKET preserves message boundaries: one recv yields one whole frame.
    const auto count = ::recv(descriptor, &received, sizeof(received), MSG_DONTWAIT);
    if (count != static_cast<ssize_t>(sizeof(received)))
    {
        return false;
    }
    if (received.Magic != CONTROL_PROTOCOL_MAGIC || received.Version != CONTROL_PROTOCOL_VERSION ||
        received.Type != static_cast<uint16>(expected))
    {
        return false;
    }
    frame = received;
    return true;
}
} // namespace

ControlState ConfigureControlEndpoint(int descriptor) noexcept
{
    if (gControlSocket != -1)
    {
        return gControlState;
    }
    const int saved_errno = errno;
    int type = 0;
    socklen_t length = sizeof(type);
    sockaddr_storage peer{};
    socklen_t peer_length = sizeof(peer);
    const bool valid = getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &type, &length) == 0 && type == SOCK_SEQPACKET &&
                       getpeername(descriptor, reinterpret_cast<sockaddr*>(&peer), &peer_length) == 0 &&
                       peer.ss_family == AF_UNIX;
    if (!valid)
    {
        gControlState = ControlState::Failed;
        errno = saved_errno;
        return gControlState;
    }
    const int owned = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
    if (owned == -1)
    {
        gControlState = ControlState::Failed;
        errno = saved_errno;
        return gControlState;
    }
    gControlSocket = owned;

    ControlFrame hello{};
    hello.Type = static_cast<uint16>(ControlMessageType::Hello);
    hello.Pid = static_cast<uint32>(::getpid());
    hello.Payload = 0;
    ssize_t sent = -1;
    for (uint32 attempt = 0; attempt < 4; ++attempt)
    {
        sent = ::send(gControlSocket, &hello, sizeof(hello), MSG_NOSIGNAL);
        if (sent >= 0 || errno != EINTR)
        {
            break;
        }
    }
    if (sent != static_cast<ssize_t>(sizeof(hello)))
    {
        gControlState = ControlState::Failed;
        errno = saved_errno;
        return gControlState;
    }

    ControlFrame ack{};
    const bool acked =
        ReceiveControlFrame(gControlSocket, ack, ControlMessageType::HelloAck, CONTROL_HANDSHAKE_TIMEOUT_MS);
    gControlState = acked ? ControlState::Ready : ControlState::Failed;
    errno = saved_errno;
    return gControlState;
}

ControlState ControlEndpointState() noexcept
{
    return gControlState;
}
} // namespace ludus::foundation::diagnostics
