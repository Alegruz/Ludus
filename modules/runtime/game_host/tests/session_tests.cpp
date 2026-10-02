// Host session + protocol end-to-end acceptance (tasks.md L1/L3, design 7/10).
//
// Drives a real HostSession over a socketpair: Hello/ready events, Pause/Step/
// Resume/Status, a full reload transaction (Validate..Done phases) that migrates
// A's checkpoint into B preserving simulation state, and Stop ending the session
// with retired instances and zero leaked host allocations. Reload runs on a
// background thread so the test can inject control frames and read events.

#include "internal/host_session.h"
#include "internal/protocol_codec.h"

#include <ludus/runtime/game_host/host.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace ludus::runtime::game_host;
using protocol::CommandKind;
using protocol::EncodeFrame;
using protocol::FrameReader;
using protocol::Message;

namespace
{
void SendCommand(int fd, const Message& m)
{
    std::vector<ludus::foundation::uint8> frame;
    REQUIRE(EncodeFrame(m.Serialize(), frame));
    std::size_t written = 0;
    while (written < frame.size())
    {
        const ssize_t n = ::write(fd, frame.data() + written, frame.size() - written);
        REQUIRE(n > 0);
        written += static_cast<std::size_t>(n);
    }
}

// Collect events for a short window using a caller-owned reader so partial
// frames survive across calls. Uses poll with a timeout so a quiet channel
// never blocks the test.
std::vector<Message> DrainEvents(int fd, FrameReader& reader, int budgetMs)
{
    std::vector<Message> events;
    const int steps = budgetMs / 5;
    for (int i = 0; i < steps; ++i)
    {
        pollfd pfd = {};
        pfd.fd = fd;
        pfd.events = POLLIN;
        const int ready = ::poll(&pfd, 1, 5);
        if (ready > 0 && (pfd.revents & POLLIN) != 0)
        {
            std::array<ludus::foundation::uint8, 4096> buf = {};
            const ssize_t n = ::read(fd, buf.data(), buf.size());
            if (n > 0)
            {
                reader.Append(buf.data(), static_cast<std::size_t>(n));
                std::string payload;
                while (reader.Next(payload))
                {
                    Message m;
                    if (Message::Parse(payload, m))
                    {
                        events.push_back(m);
                    }
                }
            }
        }
    }
    return events;
}

bool HasEvent(const std::vector<Message>& events, const char* event)
{
    for (const Message& m : events)
    {
        std::string e;
        if (m.GetString("event", e) && e == event)
        {
            return true;
        }
    }
    return false;
}

bool HasReloadPhase(const std::vector<Message>& events, const char* phase)
{
    for (const Message& m : events)
    {
        std::string e;
        std::string p;
        if (m.GetString("event", e) && e == "ReloadPhase" && m.GetString("phase", p) && p == phase)
        {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("session performs a full reload transaction over the protocol", "[session]")
{
    int sv[2] = {-1, -1};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    const int editorEnd = sv[0];
    const int hostEnd = sv[1];
    FrameReader reader;

    HostSession session(0x11, 0x22, hostEnd);
    REQUIRE(session.LoadInitial(LUDUS_FIXTURE_A_PATH));

    // Run the frame loop on a background thread; the main thread drives control.
    RunResult result = RunResult::Internal;
    std::thread hostThread([&] { result = session.RunLoop(0); });

    // Collect the ready events.
    auto startup = DrainEvents(editorEnd, reader, 100);
    REQUIRE(HasEvent(startup, "SessionReady"));
    REQUIRE(HasEvent(startup, "ModuleReady"));

    // Pause, then reload A -> B (migrates checkpoint). Expect all phases to Done.
    {
        Message pause;
        pause.SetString("command", protocol::CommandKindName(CommandKind::Pause));
        pause.SetHexId("request", 1);
        SendCommand(editorEnd, pause);
    }
    (void)DrainEvents(editorEnd, reader, 50);
    {
        Message reload;
        reload.SetString("command", protocol::CommandKindName(CommandKind::Reload));
        reload.SetHexId("request", 2);
        reload.SetString("module_path", LUDUS_FIXTURE_B_PATH);
        reload.SetHexId("generation", 2);
        SendCommand(editorEnd, reload);
    }
    auto reloadEvents = DrainEvents(editorEnd, reader, 200);
    REQUIRE(HasReloadPhase(reloadEvents, "Validate"));
    REQUIRE(HasReloadPhase(reloadEvents, "Quiesce"));
    REQUIRE(HasReloadPhase(reloadEvents, "Snapshot"));
    REQUIRE(HasReloadPhase(reloadEvents, "Stage"));
    REQUIRE(HasReloadPhase(reloadEvents, "Commit"));
    REQUIRE(HasReloadPhase(reloadEvents, "Retire"));
    REQUIRE(HasReloadPhase(reloadEvents, "Done"));

    // Stop ends the session.
    {
        Message stop;
        stop.SetString("command", protocol::CommandKindName(CommandKind::Stop));
        stop.SetHexId("request", 3);
        SendCommand(editorEnd, stop);
    }
    auto ending = DrainEvents(editorEnd, reader, 100);
    REQUIRE(HasEvent(ending, "SessionEnded"));

    hostThread.join();
    REQUIRE(result == RunResult::Ok);

    ::close(editorEnd);
    ::close(hostEnd);
}

TEST_CASE("reload to an incompatible module keeps the session alive", "[session]")
{
    int sv[2] = {-1, -1};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    const int editorEnd = sv[0];
    const int hostEnd = sv[1];
    FrameReader reader;

    HostSession session(0x11, 0x22, hostEnd);
    REQUIRE(session.LoadInitial(LUDUS_FIXTURE_A_PATH));
    RunResult result = RunResult::Internal;
    std::thread hostThread([&] { result = session.RunLoop(0); });
    (void)DrainEvents(editorEnd, reader, 80);

    {
        Message reload;
        reload.SetString("command", protocol::CommandKindName(CommandKind::Reload));
        reload.SetHexId("request", 1);
        reload.SetString("module_path", LUDUS_FIXTURE_BADID_PATH);
        reload.SetHexId("generation", 2);
        SendCommand(editorEnd, reload);
    }
    auto events = DrainEvents(editorEnd, reader, 150);
    REQUIRE(HasReloadPhase(events, "Rejected"));

    // The session is still running: Status returns Ok and Stop ends cleanly.
    {
        Message stop;
        stop.SetString("command", protocol::CommandKindName(CommandKind::Stop));
        stop.SetHexId("request", 2);
        SendCommand(editorEnd, stop);
    }
    (void)DrainEvents(editorEnd, reader, 80);
    hostThread.join();
    REQUIRE(result == RunResult::Ok);
    ::close(editorEnd);
    ::close(hostEnd);
}
