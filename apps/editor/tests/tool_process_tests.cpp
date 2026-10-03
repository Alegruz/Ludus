// Offscreen tests for ToolProcess protocol framing, credit accounting and event
// dispatch (design 8, 10). The adapter is a tiny controlled program (python) that
// emits protocol frames, so framing/decoding is exercised against a real process
// without a compiler toolchain. These use an event loop, never waitFor*.

#include "internal/controller.h"
#include "internal/play_process.h"
#include "internal/tool_process.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimer>

#include <vector>

using namespace ludus::editor;

TEST_CASE("Trusted host cleanup uncertainty blocks edits and permits explicit Stop", "[editor][play][recovery]")
{
    QTemporaryDir dir;
    REQUIRE(QDir().mkpath(dir.filePath(QStringLiteral("scripts/python"))));
    QFile adapter(dir.filePath(QStringLiteral("scripts/python/play_tool.py")));
    REQUIRE(adapter.open(QIODevice::WriteOnly));
    adapter.write(
        "import sys, json\n"
        "print(json.dumps({'protocol':1,'type':'ready'}), flush=True)\n"
        "request = json.loads(sys.stdin.readline())\n"
        "epoch = request['epoch']\n"
        "print(json.dumps({'protocol':1,'type':'result','epoch':epoch,'request':'0000000000000001',"
        "'status':'RestartRequired','host':{'state':'CleanupUnknown','generation':'0000000000000002'}}), flush=True)\n"
        "stop = json.loads(sys.stdin.readline())\n"
        "if stop['type'] == 'ack': stop = json.loads(sys.stdin.readline())\n"
        "assert stop['command']['command'] == 'Stop', stop\n"
        "print(json.dumps({'protocol':1,'type':'ended','epoch':epoch,'cleanup_confirmed':True,'reason':'Stopped'}), "
        "flush=True)\n"
        "sys.stdin.readline()\n");
    adapter.close();
    EditorController controller({});
    auto* play = controller.findChild<PlayProcess*>();
    REQUIRE(play != nullptr);
    PlayLaunch launch;
    launch.Python = QStringLiteral("python3");
    launch.ToolingRoot = dir.path();
    launch.Epoch = 1;
    launch.StartRequest = {{QStringLiteral("type"), QStringLiteral("start")}};
    REQUIRE(play->Start(launch));
    QTRY_VERIFY_WITH_TIMEOUT(controller.PlayState().Phase == PlayPhase::CleanupUnknown, 5000);
    CHECK_FALSE(controller.CanEditProperties());
    CHECK_FALSE(controller.CanBuildReload());
    CHECK_FALSE(controller.Caps().CanOpen);
    CHECK(controller.Caps().CanStop);
    controller.Stop();
    QTRY_VERIFY_WITH_TIMEOUT(controller.Caps().CanOpen, 5000);
    CHECK(controller.PlayState().Phase == PlayPhase::Stopped);
}

TEST_CASE("Unexpected Play owner teardown retires callbacks before member destruction", "[editor][play][lifetime]")
{
    QTemporaryDir dir;
    REQUIRE(QDir().mkpath(dir.filePath(QStringLiteral("scripts/python"))));
    QFile adapter(dir.filePath(QStringLiteral("scripts/python/play_tool.py")));
    REQUIRE(adapter.open(QIODevice::WriteOnly));
    adapter.write(
        "import sys, json\n"
        "print(json.dumps({'protocol':1,'type':'ready'}), flush=True)\n"
        "request = json.loads(sys.stdin.readline())\n"
        "print(json.dumps({'protocol':1,'type':'phase','epoch':request['epoch'],'phase':'Running'}), flush=True)\n"
        "sys.stdin.read()\n");
    adapter.close();
    QObject observation;
    bool tearingDown = false;
    int lateCallbacks = 0;
    {
        PlayProcess play;
        QObject::connect(&play, &PlayProcess::Finished, &observation, [&](bool) {
            if (tearingDown)
            {
                ++lateCallbacks;
            }
        });
        QObject::connect(&play, &PlayProcess::Event, &observation, [&](const QJsonObject&) {
            if (tearingDown)
            {
                ++lateCallbacks;
            }
        });
        QSignalSpy events(&play, &PlayProcess::Event);
        PlayLaunch launch;
        launch.Python = QStringLiteral("python3");
        launch.ToolingRoot = dir.path();
        launch.Epoch = 1;
        launch.StartRequest = {{QStringLiteral("type"), QStringLiteral("start")}};
        REQUIRE(play.Start(launch));
        QTRY_VERIFY_WITH_TIMEOUT(!events.isEmpty(), 5000);
        REQUIRE(play.Active());
        tearingDown = true;
    }
    CHECK(lateCallbacks == 0);
}

namespace
{
// Write a small python adapter that emits the given frames (one per line) after
// `ready`, echoing the job id. It reads the request but ignores its contents
// beyond the job. This stands in for editor_tool.py's wire behavior only.
QString WriteFakeAdapter(QTemporaryDir& dir, const QString& body)
{
    const QString path = dir.filePath(QStringLiteral("fake_adapter.py"));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    QTextStream stream(&file);
    stream << body;
    file.close();
    return path;
}

std::vector<ProtocolEvent> RunAdapter(const QString& adapterBody, ToolOperation op = ToolOperation::Configure)
{
    QTemporaryDir dir;
    const QString adapter = WriteFakeAdapter(dir, adapterBody);

    ToolProcess tool;
    std::vector<ProtocolEvent> events;
    QObject::connect(&tool, &ToolProcess::Event, &tool, [&events](const ProtocolEvent& e) { events.push_back(e); });

    ToolLaunch launch;
    launch.PythonPath = QStringLiteral("python3");
    launch.AdapterPath = adapter;
    launch.ToolingRoot = dir.path();
    launch.ProjectPath = dir.filePath(QStringLiteral("ludus.project.json"));
    launch.ExpectedSha256 = QString(64, QLatin1Char('a'));
    launch.Operation = op;
    launch.Job = 1;

    REQUIRE(tool.Start(launch));

    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&tool, &ToolProcess::Event, &loop, [&](const ProtocolEvent& e) {
        if (e.Kind == ProtocolEvent::Type::Result || e.Kind == ProtocolEvent::Type::Error)
        {
            QTimer::singleShot(50, &loop, &QEventLoop::quit);
        }
    });
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(10000);
    loop.exec();
    return events;
}
} // namespace

TEST_CASE("Ready then a well-formed result is dispatched in order", "[editor][tool]")
{
    const QString body = QStringLiteral(
        "import sys, json\n"
        "sys.stdout.write(json.dumps({'protocol':1,'type':'ready'})+'\\n'); sys.stdout.flush()\n"
        "line = sys.stdin.readline()\n"
        "job = json.loads(line)['job']\n"
        "sys.stdout.write(json.dumps({'protocol':1,'job':job,'type':'phase','stage':'configuring'})+'\\n')\n"
        "sys.stdout.write(json.dumps({'protocol':1,'job':job,'type':'result','outcome':'success',"
        "'stage':'configuring','code':'Ok','message':'done','cleanup_confirmed':True,"
        "'exit_code':0,'signal':None})+'\\n')\n"
        "sys.stdout.flush()\n");
    const auto events = RunAdapter(body);
    REQUIRE(events.size() >= 3);
    CHECK(events.front().Kind == ProtocolEvent::Type::Ready);
    bool sawResult = false;
    for (const ProtocolEvent& e : events)
    {
        if (e.Kind == ProtocolEvent::Type::Result)
        {
            sawResult = true;
            CHECK(e.Outcome == QStringLiteral("success"));
            CHECK(e.Code == ResultCode::Ok);
            CHECK(e.CleanupConfirmed);
        }
    }
    CHECK(sawResult);
}

TEST_CASE("A job mismatch is a protocol error", "[editor][tool]")
{
    const QString body =
        QStringLiteral("import sys, json\n"
                       "sys.stdout.write(json.dumps({'protocol':1,'type':'ready'})+'\\n'); sys.stdout.flush()\n"
                       "sys.stdin.readline()\n"
                       "sys.stdout.write(json.dumps({'protocol':1,'job':'000000000000ffff','type':'phase',"
                       "'stage':'configuring'})+'\\n'); sys.stdout.flush()\n");
    const auto events = RunAdapter(body);
    bool sawError = false;
    for (const ProtocolEvent& e : events)
    {
        if (e.Kind == ProtocolEvent::Type::Error && e.Code == ResultCode::ProtocolError)
        {
            sawError = true;
        }
    }
    CHECK(sawError);
}

TEST_CASE("Unknown event type is rejected in version 1", "[editor][tool]")
{
    const QString body = QStringLiteral(
        "import sys, json\n"
        "sys.stdout.write(json.dumps({'protocol':1,'type':'ready'})+'\\n'); sys.stdout.flush()\n"
        "job = json.loads(sys.stdin.readline())['job']\n"
        "sys.stdout.write(json.dumps({'protocol':1,'job':job,'type':'mystery'})+'\\n'); sys.stdout.flush()\n");
    const auto events = RunAdapter(body);
    bool sawError = false;
    for (const ProtocolEvent& e : events)
    {
        if (e.Kind == ProtocolEvent::Type::Error && e.Code == ResultCode::ProtocolError)
        {
            sawError = true;
        }
    }
    CHECK(sawError);
}

TEST_CASE("Exit without a terminal result is never success", "[editor][tool]")
{
    const QString body =
        QStringLiteral("import sys, json\n"
                       "sys.stdout.write(json.dumps({'protocol':1,'type':'ready'})+'\\n'); sys.stdout.flush()\n"
                       "sys.stdin.readline()\n"
                       "sys.exit(0)\n");
    const auto events = RunAdapter(body);
    bool sawCleanupUnknown = false;
    for (const ProtocolEvent& e : events)
    {
        if (e.Kind == ProtocolEvent::Type::Error && e.Code == ResultCode::CleanupUnknown)
        {
            sawCleanupUnknown = true;
            CHECK_FALSE(e.CleanupConfirmed);
        }
    }
    CHECK(sawCleanupUnknown);
}

TEST_CASE("Output frames carry text and a monotonic end_offset", "[editor][tool]")
{
    const QString body =
        QStringLiteral("import sys, json\n"
                       "sys.stdout.write(json.dumps({'protocol':1,'type':'ready'})+'\\n'); sys.stdout.flush()\n"
                       "job = json.loads(sys.stdin.readline())['job']\n"
                       "sys.stdout.write(json.dumps({'protocol':1,'job':job,'type':'output','stage':'building',"
                       "'stream':'stdout','text':'hello','end_offset':'0000000000000010'})+'\\n')\n"
                       "sys.stdout.write(json.dumps({'protocol':1,'job':job,'type':'result','outcome':'success',"
                       "'stage':'building','code':'Ok','message':'','cleanup_confirmed':True,'exit_code':0,"
                       "'signal':None})+'\\n'); sys.stdout.flush()\n");
    const auto events = RunAdapter(body, ToolOperation::Build);
    bool sawOutput = false;
    for (const ProtocolEvent& e : events)
    {
        if (e.Kind == ProtocolEvent::Type::Output)
        {
            sawOutput = true;
            CHECK(e.Text == QStringLiteral("hello"));
            CHECK(e.EndOffset == 0x10);
        }
    }
    CHECK(sawOutput);
}
