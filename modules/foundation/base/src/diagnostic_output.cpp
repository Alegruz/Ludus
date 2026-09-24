#include <ludus/foundation/base/diagnostic_output.hpp>

#include <cerrno>
#include <cstdlib>
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
void PutU16(char* buffer, usize offset, uint16 value) noexcept
{
    buffer[offset + 0] = static_cast<char>(value & 0xFFu);
    buffer[offset + 1] = static_cast<char>((value >> 8) & 0xFFu);
}

void PutU32(char* buffer, usize offset, uint32 value) noexcept
{
    buffer[offset + 0] = static_cast<char>(value & 0xFFu);
    buffer[offset + 1] = static_cast<char>((value >> 8) & 0xFFu);
    buffer[offset + 2] = static_cast<char>((value >> 16) & 0xFFu);
    buffer[offset + 3] = static_cast<char>((value >> 24) & 0xFFu);
}

uint16 GetU16(const char* buffer, usize offset) noexcept
{
    return static_cast<uint16>(static_cast<uint8>(buffer[offset + 0])) |
           static_cast<uint16>(static_cast<uint16>(static_cast<uint8>(buffer[offset + 1])) << 8);
}

uint32 GetU32(const char* buffer, usize offset) noexcept
{
    return static_cast<uint32>(static_cast<uint8>(buffer[offset + 0])) |
           (static_cast<uint32>(static_cast<uint8>(buffer[offset + 1])) << 8) |
           (static_cast<uint32>(static_cast<uint8>(buffer[offset + 2])) << 16) |
           (static_cast<uint32>(static_cast<uint8>(buffer[offset + 3])) << 24);
}

// Bounded, EINTR-aware receive of exactly one framed message within the deadline.
// Returns true only for a complete, well-formed header of the expected kind with
// no trailing payload (Hello/HelloAck carry none). Never blocks past the
// deadline; a slow/absent/malformed helper fails.
bool ReceiveControlMessage(int descriptor, ControlMessageType expected, int timeout_ms) noexcept
{
    pollfd waiter{};
    waiter.fd = descriptor;
    waiter.events = POLLIN;
    const int ready = ::poll(&waiter, 1, timeout_ms);
    if (ready <= 0 || (waiter.revents & POLLIN) == 0)
    {
        return false;
    }
    char frame[CONTROL_HEADER_SIZE + CONTROL_MAX_PAYLOAD];
    // SEQPACKET preserves message boundaries: one recv yields one whole message.
    const auto count = ::recv(descriptor, frame, sizeof(frame), MSG_DONTWAIT);
    if (count < static_cast<ssize_t>(CONTROL_HEADER_SIZE))
    {
        return false;
    }
    ControlHeader header{};
    if (!DecodeControlHeader(frame, static_cast<usize>(count), header))
    {
        return false;
    }
    // Handshake frames carry no payload and no incident id.
    return header.Kind == static_cast<uint16>(expected) && header.IncidentId == 0 && header.Length == 0 &&
           static_cast<usize>(count) == CONTROL_HEADER_SIZE;
}
} // namespace

usize EncodeControlHeader(char* buffer,
                          usize capacity,
                          ControlMessageType kind,
                          uint32 incidentId,
                          uint32 length) noexcept
{
    if (buffer == nullptr || capacity < CONTROL_HEADER_SIZE || length > CONTROL_MAX_PAYLOAD)
    {
        return 0;
    }
    PutU32(buffer, 0, CONTROL_PROTOCOL_MAGIC);
    PutU16(buffer, 4, CONTROL_PROTOCOL_VERSION);
    PutU16(buffer, 6, static_cast<uint16>(kind));
    PutU32(buffer, 8, incidentId);
    PutU32(buffer, 12, length);
    return CONTROL_HEADER_SIZE;
}

bool DecodeControlHeader(const char* buffer, usize size, ControlHeader& header) noexcept
{
    if (buffer == nullptr || size < CONTROL_HEADER_SIZE)
    {
        return false;
    }
    if (GetU32(buffer, 0) != CONTROL_PROTOCOL_MAGIC || GetU16(buffer, 4) != CONTROL_PROTOCOL_VERSION)
    {
        return false;
    }
    const uint16 kind = GetU16(buffer, 6);
    if (kind < static_cast<uint16>(ControlMessageType::Hello) ||
        kind > static_cast<uint16>(ControlMessageType::DecisionReply))
    {
        return false;
    }
    const uint32 length = GetU32(buffer, 12);
    if (length > CONTROL_MAX_PAYLOAD || CONTROL_HEADER_SIZE + length > size)
    {
        return false;
    }
    header.Kind = kind;
    header.IncidentId = GetU32(buffer, 8);
    header.Length = length;
    return true;
}

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

    char hello[CONTROL_HEADER_SIZE];
    const usize hello_size = EncodeControlHeader(hello, sizeof(hello), ControlMessageType::Hello, 0, 0);
    ssize_t sent = -1;
    for (uint32 attempt = 0; attempt < 4; ++attempt)
    {
        sent = ::send(gControlSocket, hello, hello_size, MSG_NOSIGNAL);
        if (sent >= 0 || errno != EINTR)
        {
            break;
        }
    }
    if (sent != static_cast<ssize_t>(hello_size))
    {
        gControlState = ControlState::Failed;
        errno = saved_errno;
        return gControlState;
    }

    const bool acked =
        ReceiveControlMessage(gControlSocket, ControlMessageType::HelloAck, CONTROL_HANDSHAKE_TIMEOUT_MS);
    gControlState = acked ? ControlState::Ready : ControlState::Failed;
    errno = saved_errno;
    return gControlState;
}

ControlState ControlEndpointState() noexcept
{
    return gControlState;
}

bool IsContinuousIntegration() noexcept
{
    const int saved_errno = errno;
    // Common CI signals. The generic CI variable covers GitHub Actions, GitLab,
    // CircleCI, Travis, and others; the rest catch environments that omit it.
    static const char* const signals[] =
        {"CI", "CONTINUOUS_INTEGRATION", "GITHUB_ACTIONS", "GITLAB_CI", "BUILDKITE", "JENKINS_URL", "TEAMCITY_VERSION"};
    bool detected = false;
    for (const char* signal : signals)
    {
        const char* value = ::getenv(signal);
        if (value != nullptr && value[0] != '\0')
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

ControlDecision RequestAssertDecision(uint32 incidentId, const char* report, usize size) noexcept
{
    if (gControlState != ControlState::Ready || gControlSocket == -1)
    {
        return ControlDecision::Terminate;
    }
    if (size > CONTROL_MAX_PAYLOAD)
    {
        size = CONTROL_MAX_PAYLOAD; // Bound the request; a longer report is clipped.
    }
    const int saved_errno = errno;

    // Build request: header + bounded owned report bytes. One SEQPACKET message.
    char frame[CONTROL_HEADER_SIZE + CONTROL_MAX_PAYLOAD];
    const usize header_size = EncodeControlHeader(frame,
                                                  sizeof(frame),
                                                  ControlMessageType::DecisionRequest,
                                                  incidentId,
                                                  static_cast<uint32>(report != nullptr ? size : 0));
    usize frame_size = header_size;
    if (report != nullptr && size != 0)
    {
        for (usize i = 0; i < size; ++i)
        {
            frame[header_size + i] = report[i];
        }
        frame_size += size;
    }

    ssize_t sent = -1;
    for (uint32 attempt = 0; attempt < 4; ++attempt)
    {
        sent = ::send(gControlSocket, frame, frame_size, MSG_NOSIGNAL);
        if (sent >= 0 || errno != EINTR)
        {
            break;
        }
    }
    if (sent != static_cast<ssize_t>(frame_size))
    {
        errno = saved_errno;
        return ControlDecision::Terminate; // Helper gone / short send => never continue.
    }

    // Block for the reply. No timeout: a healthy dialog waits for a human. A
    // closed helper makes recv return 0 (=> Terminate); EINTR is retried.
    ControlDecision decision = ControlDecision::Terminate;
    for (;;)
    {
        char reply[CONTROL_HEADER_SIZE + CONTROL_MAX_PAYLOAD];
        const auto count = ::recv(gControlSocket, reply, sizeof(reply), 0);
        if (count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break; // Transport error => Terminate.
        }
        if (count == 0)
        {
            break; // Helper closed the channel => Terminate.
        }
        ControlHeader header{};
        if (!DecodeControlHeader(reply, static_cast<usize>(count), header))
        {
            break; // Malformed => Terminate; do not loop on garbage.
        }
        // Only the active incident's explicit ContinueOnce authorizes a resume.
        if (header.Kind == static_cast<uint16>(ControlMessageType::DecisionReply) && header.IncidentId == incidentId &&
            header.Length == 1 &&
            static_cast<uint8>(reply[CONTROL_HEADER_SIZE]) == static_cast<uint8>(ControlDecision::ContinueOnce))
        {
            decision = ControlDecision::ContinueOnce;
        }
        break; // A stale/duplicate/wrong-id/wrong-kind reply falls through as Terminate.
    }
    errno = saved_errno;
    return decision;
}
} // namespace ludus::foundation::diagnostics
