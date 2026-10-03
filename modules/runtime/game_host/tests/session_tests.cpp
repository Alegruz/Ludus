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
#include <utility>
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
uint64 CurrentSession = 0;
uint64 CurrentGeneration = 1;
class HostWorker
{
public:
    template <typename Fn>
    HostWorker(int fd, Fn&& fn) : Fd_(fd), Thread_(std::forward<Fn>(fn))
    {
    }
    ~HostWorker()
    {
        if (Thread_.joinable())
        {
            (void)::shutdown(Fd_, SHUT_RDWR);
            Thread_.join();
        }
    }
    void join()
    {
        Thread_.join();
    }

private:
    int Fd_ = -1;
    std::thread Thread_;
};
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void SendCommand(int fd, const Message& m)
{
    std::vector<ludus::foundation::uint8> frame;
    Message command = m;
    command.SetUint("protocol", protocol::kProtocolVersion);
    command.SetHexId("session", CurrentSession);
    command.SetHexId("epoch", 1);
    std::string type;
    (void)command.GetString("command", type);
    if (type == "Pause" || type == "Resume" || type == "Step" || type == "Reload" || type == "ReadProperties" ||
        type == "ApplyEdits")
    {
        if (!command.Has("expected_generation"))
        {
            command.SetHexId("expected_generation", CurrentGeneration);
        }
    }
    REQUIRE(EncodeFrame(command.Serialize(), frame));
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
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
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
                        (void)m.GetHexId("session", CurrentSession);
                        std::string event;
                        std::string phase;
                        (void)m.GetString("event", event);
                        (void)m.GetString("phase", phase);
                        if (event == "ModuleReady" || (event == "ReloadPhase" && phase == "Done"))
                        {
                            (void)m.GetHexId("generation", CurrentGeneration);
                        }
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

bool HasStatus(const std::vector<Message>& events, uint64 request, const char* status)
{
    for (const auto& event : events)
    {
        uint64 id = 0;
        std::string value;
        if (event.GetHexId("request", id) && id == request && event.GetString("status", value) && value == status)
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
    HostWorker hostThread(editorEnd, [&] { result = session.RunLoop(0); });

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

TEST_CASE("property batches are conditional, atomic and replayable across reload", "[session][properties]")
{
    int sv[2] = {-1, -1};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    FrameReader reader;
    HostSession session(0x11, 0x22, sv[1]);
    REQUIRE(session.LoadInitial(LUDUS_FIXTURE_A_PATH));
    RunResult result = RunResult::Internal;
    HostWorker worker(sv[0], [&] { result = session.RunLoop(0); });
    (void)DrainEvents(sv[0], reader, 60);
    auto command = [](std::string_view name, uint64 request) {
        Message message;
        message.SetString("command", name);
        message.SetHexId("request", request);
        return message;
    };
    auto expectStatus = [](const std::vector<Message>& events, uint64 request, std::string_view expected) {
        bool found = false;
        for (const auto& event : events)
        {
            uint64 id = 0;
            std::string status;
            if (event.GetHexId("request", id) && id == request && event.GetString("status", status))
            {
                CHECK(status == expected);
                found = true;
            }
        }
        REQUIRE(found);
    };
    SendCommand(sv[0], command("Pause", 1));
    expectStatus(DrainEvents(sv[0], reader, 60), 1, "Ok");
    auto read = [&](uint64 request) {
        SendCommand(sv[0], command("ReadProperties", request));
        auto events = DrainEvents(sv[0], reader, 60);
        expectStatus(events, request, "Ok");
        std::vector<Message> properties;
        for (const auto& event : events)
        {
            std::vector<Message> rows;
            if (event.GetRecords("properties", rows))
            {
                properties.insert(properties.end(), rows.begin(), rows.end());
            }
        }
        REQUIRE(properties.size() == 3);
        return properties;
    };
    const auto before = read(2);
    uint64 revision = 0;
    REQUIRE(before[0].GetHexId("revision", revision));
    Message speed;
    speed.SetHexId("object", 0x1001);
    speed.SetHexId("property", 0x2001);
    speed.SetHexId("revision", revision);
    speed.SetUint("kind", 2);
    speed.SetUint("bits", 0x40000000); // Exactly 2.0f.
    Message label;
    label.SetHexId("object", 0x1001);
    label.SetHexId("property", 0x2003);
    label.SetHexId("revision", revision);
    label.SetUint("kind", 4);
    label.SetString("value", "edited é");
    auto batch = [&](uint64 request, const std::vector<Message>& edits) {
        auto message = command("ApplyEdits", request);
        message.SetUint("schema_epoch", 0);
        message.SetUint("schema_version", 1);
        message.SetRecords("edits", edits);
        return message;
    };
    Message bad = label;
    bad.SetString("value", std::string(32, 'x')); // Module-specific 31-byte bound.
    SendCommand(sv[0], batch(3, {speed, bad}));
    expectStatus(DrainEvents(sv[0], reader, 60), 3, "InvalidRequest");
    auto unchanged = read(4);
    for (usize i = 0; i < before.size(); ++i)
    {
        CHECK(unchanged[i].Serialize() == before[i].Serialize());
    }
    auto edit = batch(5, {speed, label});
    SendCommand(sv[0], edit);
    expectStatus(DrainEvents(sv[0], reader, 60), 5, "Ok");
    const auto after = read(6);
    uint64 bits = 0;
    uint64 editedRevision = 0;
    REQUIRE(after[0].GetUint("bits", bits));
    CHECK(bits == 0x40000000);
    REQUIRE(after[0].GetHexId("revision", editedRevision));
    CHECK(editedRevision == revision + 1);
    SendCommand(sv[0], edit); // Lost reply retry must not advance revision twice.
    expectStatus(DrainEvents(sv[0], reader, 60), 5, "Ok");
    const auto retry = read(7);
    REQUIRE(retry[0].GetHexId("revision", editedRevision));
    CHECK(editedRevision == revision + 1);
    SendCommand(sv[0], batch(8, {speed}));
    expectStatus(DrainEvents(sv[0], reader, 60), 8, "StaleRevision");
    Message nonfinite = speed;
    nonfinite.SetHexId("revision", editedRevision);
    nonfinite.SetUint("bits", 0x7FC00000);
    SendCommand(sv[0], batch(9, {nonfinite}));
    expectStatus(DrainEvents(sv[0], reader, 60), 9, "InvalidRequest");
    auto reload = command("Reload", 10);
    reload.SetString("module_path", LUDUS_FIXTURE_B_PATH);
    reload.SetHexId("generation", 2);
    SendCommand(sv[0], reload);
    expectStatus(DrainEvents(sv[0], reader, 100), 10, "Ok");
    auto staleSchema = batch(11, {speed});
    SendCommand(sv[0], staleSchema);
    expectStatus(DrainEvents(sv[0], reader, 60), 11, "SchemaChanged");
    auto oldGeneration = command("Pause", 12);
    oldGeneration.SetHexId("expected_generation", 1);
    SendCommand(sv[0], oldGeneration);
    expectStatus(DrainEvents(sv[0], reader, 60), 12, "SchemaChanged");
    auto unknownField = command("Resume", 13);
    unknownField.SetUint("surprise", 1);
    SendCommand(sv[0], unknownField);
    expectStatus(DrainEvents(sv[0], reader, 60), 13, "InvalidRequest");
    auto status = command("Status", 14);
    status.SetHexId("reconcile", 5);
    SendCommand(sv[0], status);
    auto events = DrainEvents(sv[0], reader, 60);
    bool reconciled = false;
    for (const auto& event : events)
    {
        std::string outcome;
        if (event.GetString("reconciled_status", outcome))
        {
            CHECK(outcome == "Ok");
            reconciled = true;
        }
    }
    CHECK(reconciled);
    SendCommand(sv[0], command("Stop", 15));
    (void)DrainEvents(sv[0], reader, 80);
    worker.join();
    CHECK(result == RunResult::Ok);
    ::close(sv[0]);
    ::close(sv[1]);
}

namespace
{
// Pull a uint field out of the newest Status CommandResult in a drained batch.
bool LatestStatusUint(const std::vector<Message>& events, const char* field, ludus::foundation::uint64& out)
{
    bool found = false;
    for (const Message& m : events)
    {
        std::string e;
        ludus::foundation::uint64 v = 0;
        if (m.GetString("event", e) && e == "CommandResult" && m.GetUint(field, v))
        {
            out = v;
            found = true;
        }
    }
    return found;
}
} // namespace

TEST_CASE("pause then step advances exactly one simulation tick (no catch-up)", "[session][pause]")
{
    int sv[2] = {-1, -1};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    const int editorEnd = sv[0];
    const int hostEnd = sv[1];
    FrameReader reader;

    HostSession session(0x11, 0x22, hostEnd);
    REQUIRE(session.LoadInitial(LUDUS_FIXTURE_A_PATH));
    RunResult result = RunResult::Internal;
    HostWorker hostThread(editorEnd, [&] { result = session.RunLoop(0); });
    (void)DrainEvents(editorEnd, reader, 80);

    // Pause.
    {
        Message pause;
        pause.SetString("command", protocol::CommandKindName(CommandKind::Pause));
        pause.SetHexId("request", 1);
        SendCommand(editorEnd, pause);
    }
    (void)DrainEvents(editorEnd, reader, 60);

    // Status: record sim ticks while paused.
    {
        Message status;
        status.SetString("command", protocol::CommandKindName(CommandKind::Status));
        status.SetHexId("request", 2);
        SendCommand(editorEnd, status);
    }
    auto s1 = DrainEvents(editorEnd, reader, 60);
    ludus::foundation::uint64 ticksPaused = 0;
    REQUIRE(LatestStatusUint(s1, "sim_ticks", ticksPaused));

    // Let the loop spin while paused; ticks must NOT advance (no catch-up).
    (void)DrainEvents(editorEnd, reader, 60);
    {
        Message status;
        status.SetString("command", protocol::CommandKindName(CommandKind::Status));
        status.SetHexId("request", 3);
        SendCommand(editorEnd, status);
    }
    auto s2 = DrainEvents(editorEnd, reader, 60);
    ludus::foundation::uint64 ticksStillPaused = 0;
    REQUIRE(LatestStatusUint(s2, "sim_ticks", ticksStillPaused));
    REQUIRE(ticksStillPaused == ticksPaused);

    // One Step: exactly one additional simulation tick.
    {
        Message step;
        step.SetString("command", protocol::CommandKindName(CommandKind::Step));
        step.SetHexId("request", 4);
        SendCommand(editorEnd, step);
    }
    (void)DrainEvents(editorEnd, reader, 60);
    {
        Message status;
        status.SetString("command", protocol::CommandKindName(CommandKind::Status));
        status.SetHexId("request", 5);
        SendCommand(editorEnd, status);
    }
    auto s3 = DrainEvents(editorEnd, reader, 60);
    ludus::foundation::uint64 ticksAfterStep = 0;
    REQUIRE(LatestStatusUint(s3, "sim_ticks", ticksAfterStep));
    REQUIRE(ticksAfterStep == ticksPaused + 1);

    {
        Message stop;
        stop.SetString("command", protocol::CommandKindName(CommandKind::Stop));
        stop.SetHexId("request", 6);
        SendCommand(editorEnd, stop);
    }
    (void)DrainEvents(editorEnd, reader, 80);
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
    HostWorker hostThread(editorEnd, [&] { result = session.RunLoop(0); });
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

TEST_CASE("duplicate Step is applied once and acknowledged IDs are never replayed", "[session][protocol]")
{
    int sv[2] = {-1, -1};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    const int editorEnd = sv[0];
    HostSession session(1, 1, sv[1]);
    REQUIRE(session.LoadInitial(LUDUS_FIXTURE_A_PATH));
    HostWorker worker(editorEnd, [&] { (void)session.RunLoop(0); });
    FrameReader reader;
    (void)DrainEvents(editorEnd, reader, 50);
    Message pause;
    pause.SetString("command", "Pause");
    pause.SetHexId("request", 1);
    SendCommand(editorEnd, pause);
    (void)DrainEvents(editorEnd, reader, 50);
    Message status;
    status.SetString("command", "Status");
    status.SetHexId("request", 2);
    SendCommand(editorEnd, status);
    auto before = DrainEvents(editorEnd, reader, 50);
    uint64 ticks = 0;
    REQUIRE(LatestStatusUint(before, "sim_ticks", ticks));
    Message step;
    step.SetString("command", "Step");
    step.SetHexId("request", 3);
    SendCommand(editorEnd, step);
    SendCommand(editorEnd, step);
    (void)DrainEvents(editorEnd, reader, 50);
    status.SetHexId("request", 4);
    SendCommand(editorEnd, status);
    auto after = DrainEvents(editorEnd, reader, 50);
    uint64 advanced = 0;
    REQUIRE(LatestStatusUint(after, "sim_ticks", advanced));
    REQUIRE(advanced == ticks + 1);
    Message mismatch = step;
    mismatch.SetString("command", "Resume");
    SendCommand(editorEnd, mismatch);
    REQUIRE(HasStatus(DrainEvents(editorEnd, reader, 50), 3, "InvalidRequest"));
    Message ack;
    ack.SetString("command", "Ack");
    ack.SetHexId("request", 5);
    ack.SetHexId("acknowledge", 3);
    SendCommand(editorEnd, ack);
    (void)DrainEvents(editorEnd, reader, 50);
    SendCommand(editorEnd, step);
    REQUIRE(HasStatus(DrainEvents(editorEnd, reader, 50), 3, "InvalidRequest"));
    Message stop;
    stop.SetString("command", "Stop");
    stop.SetHexId("request", 6);
    SendCommand(editorEnd, stop);
    (void)DrainEvents(editorEnd, reader, 80);
    worker.join();
    ::close(editorEnd);
    ::close(sv[1]);
}

TEST_CASE("Stop is bounded under output backpressure without corrupting frames", "[session][backpressure]")
{
    int sv[2] = {-1, -1};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int smallBuffer = 1024;
    REQUIRE(::setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &smallBuffer, sizeof(smallBuffer)) == 0);
    const int editorEnd = sv[0];
    HostSession session(1, 1, sv[1]);
    REQUIRE(session.LoadInitial(LUDUS_FIXTURE_A_PATH));
    RunResult result = RunResult::Internal;
    HostWorker worker(editorEnd, [&] { result = session.RunLoop(0); });
    FrameReader reader;
    (void)DrainEvents(editorEnd, reader, 50);
    for (uint64 i = 1; i <= 512; ++i)
    {
        Message status;
        status.SetString("command", "Status");
        status.SetHexId("request", i);
        SendCommand(editorEnd, status);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    Message stop;
    stop.SetString("command", "Stop");
    stop.SetHexId("request", 513);
    SendCommand(editorEnd, stop);
    auto ending = DrainEvents(editorEnd, reader, 1200);
    worker.join();
    CHECK_FALSE(reader.Failed());
    CHECK(HasEvent(ending, "SessionEnded"));
    CHECK(result == RunResult::Ok);
    ::close(editorEnd);
    ::close(sv[1]);
}
