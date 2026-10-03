// Offscreen tests for ToolProcess protocol framing, credit accounting and event
// dispatch (design 8, 10). The adapter is a tiny controlled program (python) that
// emits protocol frames, so framing/decoding is exercised against a real process
// without a compiler toolchain. These use an event loop, never waitFor*.

#include "internal/tool_process.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <vector>

using namespace ludus::editor;

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

TEST_CASE("A debugger frame cannot claim game Running or escape its operation", "[editor][debug][protocol]")
{
    const QString prefix = QStringLiteral("import json, sys\n"
                                          "print(json.dumps({'protocol':1, 'type':'ready'}), flush=True)\n"
                                          "r=json.loads(sys.stdin.readline())\n"
                                          "e={'protocol':1, 'job':r['job'], 'pid':123, 'executable':'/game'}\n");
    for (const QString& body : {
             QStringLiteral("e['type']='runtime_started'\n"),
             QStringLiteral("e.update(type='debugger_started', pid=-1)\n"),
             QStringLiteral("e.update(type='debugger_started', pid=1.5)\n"),
         })
    {
        const auto events =
            RunAdapter(prefix + body + QStringLiteral("print(json.dumps(e), flush=True)\n"), ToolOperation::BuildDebug);
        bool rejected = false;
        for (const auto& event : events)
        {
            rejected = rejected || event.Kind == ProtocolEvent::Type::Error;
            CHECK(event.Kind != ProtocolEvent::Type::DebuggerStarted);
            CHECK(event.Kind != ProtocolEvent::Type::RuntimeStarted);
        }
        CHECK(rejected);
    }
    const auto events =
        RunAdapter(prefix + QStringLiteral("e['type']='debugger_started'\nprint(json.dumps(e), flush=True)\n"));
    bool rejected = false;
    for (const auto& event : events)
    {
        rejected = rejected || event.Kind == ProtocolEvent::Type::Error;
    }
    CHECK(rejected);
}
