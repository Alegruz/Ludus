#include "internal/play_process.h"
#include "internal/source_watch.h"
#include "internal/tool_process.h"

// Thanks to Qt Group, "Qt for WebAssembly", browser sandbox and event-loop
// constraints: a static hosted editor has no native process/tooling transport.
// Keep desktop processes behind an adapter; never report a simulated build.
// https://doc.qt.io/qt-6/wasm.html
namespace ludus::editor
{
ToolProcess::ToolProcess(QObject* parent) : QObject(parent) {}
ToolProcess::~ToolProcess() = default;
bool ToolProcess::Start(const ToolLaunch&)
{
    return false;
}
void ToolProcess::Cancel() {}
PlayProcess::PlayProcess(QObject* parent) : QObject(parent) {}
PlayProcess::~PlayProcess() = default;
// Preserve the native ownership-taking signatures for the unavailable adapter.
// NOLINTNEXTLINE(performance-unnecessary-value-param)
bool PlayProcess::Start(PlayLaunch)
{
    return false;
}
// NOLINTNEXTLINE(performance-unnecessary-value-param) -- Native adapter consumes the request.
bool PlayProcess::BeginSession(foundation::uint64, QJsonObject)
{
    return false;
}
// NOLINTNEXTLINE(performance-unnecessary-value-param) -- Native adapter mutates its owned request.
bool PlayProcess::Send(QJsonObject)
{
    return false;
}
void PlayProcess::Close() {}
SourceWatch::SourceWatch(QObject* parent) : QObject(parent) {}
bool SourceWatch::Start(const QString&)
{
    return false;
}
void SourceWatch::Stop()
{
    Project_.clear();
}
void SourceWatch::SetBusy(bool) {}
} // namespace ludus::editor
