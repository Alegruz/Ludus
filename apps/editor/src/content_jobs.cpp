#include "internal/content_browser.h"
#include "internal/content_import.h"

#include <QByteArray>
#include <QSharedPointer>
#if !defined(Q_OS_WASM)
#    include <QThread>
#endif

#include <atomic>
#include <new>

namespace ludus::editor
{
namespace
{
using namespace foundation;
struct JobState final
{
    ContentImportGate Gate;
    std::atomic<bool> Done{false};
    ContentResult Result;
};
#if !defined(Q_OS_WASM)
void Run(JobState& state)
{
    auto& result = state.Result;
    const auto& request = result.Request;
    if (!request.Id.isEmpty())
    {
        const auto imported = ImportAudioSource({ .Root = request.Root, .File = request.File, .Id = request.Id },
                                                state.Gate,
                                                request.CheckCatalog ? &request.CatalogDigest : nullptr);
        result.Status = imported.Status;
        result.Message = imported.Message;
        result.CandidatePath = imported.CandidatePath;
    }
    else if (state.Gate.Cancelled())
    {
        result.Status = content::Status::Cancelled;
        result.Message = QStringLiteral("Catalog refresh cancelled.");
    }
    else
    {
        content::Bytes bytes;
        content::Catalog catalog;
        content::Diagnostic diagnostic;
        const auto root = request.Root.toUtf8();
        result.Status = content::ReadFile({root.constData(), static_cast<usize>(root.size())},
                                          "catalog.json",
                                          content::MAX_DOCUMENT_BYTES,
                                          bytes);
        if (result.Status == content::Status::Ok)
        {
            result.Status = catalog.Read(bytes.String(), diagnostic);
        }
        if (state.Gate.Cancelled())
        {
            result.Status = content::Status::Cancelled;
        }
        if (result.Status == content::Status::Ok)
        {
            result.CatalogDigest = content::Hash(bytes.Data());
            result.Rows.reserve(static_cast<qsizetype>(catalog.Entries().size()));
            for (const auto& entry : catalog.Entries())
            {
                result.Rows.append({QString::fromUtf8(entry.Id.Data, static_cast<qsizetype>(entry.Id.Length)),
                                    QString::fromUtf8(entry.Path.Data, static_cast<qsizetype>(entry.Path.Length)),
                                    entry.Type});
            }
            result.Message =
                QStringLiteral("%1 resources · select an ID to inspect or reimport.").arg(result.Rows.size());
        }
        else
        {
            result.Message =
                result.Status == content::Status::NotFound
                    ? QStringLiteral("No catalog yet. Import WAV or FLAC to create one.")
                : result.Status == content::Status::Cancelled
                    ? QStringLiteral("Catalog refresh cancelled; last valid list retained.")
                    : QStringLiteral("Catalog refresh failed; last valid list retained. Check content/catalog.json.");
        }
    }
    state.Done.store(true, std::memory_order_release);
}
#endif
} // namespace

struct ContentJobs::Impl final
{
    QSharedPointer<JobState> State;
#if !defined(Q_OS_WASM)
    QThread* Thread = nullptr;
#endif
};
ContentJobs::ContentJobs(QObject* parent) : QObject(parent), Impl_(new(std::nothrow) Impl)
{
    PollTimer_.setInterval(20);
    connect(&PollTimer_, &QTimer::timeout, this, &ContentJobs::Poll);
}
ContentJobs::~ContentJobs()
{
    Shutdown();
#if !defined(Q_OS_WASM)
    if (Impl_ != nullptr && Impl_->Thread != nullptr)
    {
        if (!Impl_->Thread->isFinished())
        {
            connect(Impl_->Thread, &QThread::finished, Impl_->Thread, &QObject::deleteLater);
        }
        if (Impl_->Thread->isFinished())
        {
            delete Impl_->Thread;
        }
    }
#endif
    delete Impl_;
}
bool ContentJobs::Start(const ContentRequest& request)
{
    if (Impl_ == nullptr || Closing_ || Busy())
    {
        return false;
    }
    auto state = QSharedPointer<JobState>(new (std::nothrow) JobState);
    if (state.isNull())
    {
        return false;
    }
    state->Result.Request = request;
    Impl_->State = state;
#if !defined(Q_OS_WASM)
    // Thanks to Qt Group, QThread, "Managing Threads" and create(): separate
    // worker execution from QObject affinity. Poll copied state during work;
    // ordinary shutdown acknowledges completion without blocking a GUI wait.
    // https://doc.qt.io/qt-6/qthread.html
    // The thread never captures this QObject or touches any model. Its copied
    // state outlives cancellation/destruction; completion is polled on the UI.
    auto* thread = QThread::create([state]() { Run(*state); });
    Impl_->Thread = thread;
    thread->start();
    if (!thread->isRunning() && !state->Done.load(std::memory_order_acquire))
    {
        delete thread;
        Impl_->Thread = nullptr;
        state->Result.Status = content::Status::IoError;
        state->Result.Message = QStringLiteral("Content worker could not start; retry the operation.");
        state->Done.store(true, std::memory_order_release);
    }
#else
    state->Result.Status = content::Status::Unsupported;
    state->Result.Message = QStringLiteral("Content browsing and audio import require the desktop editor.");
    state->Done.store(true, std::memory_order_release);
#endif
    PollTimer_.start();
    Q_EMIT BusyChanged();
    return true;
}
bool ContentJobs::Cancel()
{
    return Impl_ != nullptr && !Impl_->State.isNull() && !Impl_->State->Done.load(std::memory_order_acquire) &&
           Impl_->State->Gate.Cancel();
}
void ContentJobs::Shutdown()
{
    Closing_ = true;
    (void)Cancel();
}
bool ContentJobs::Busy() const noexcept
{
    return Impl_ != nullptr && !Impl_->State.isNull();
}
bool ContentJobs::Importing() const noexcept
{
    return Busy() && !Impl_->State->Result.Request.Id.isEmpty();
}
bool ContentJobs::Finished() const noexcept
{
    return !Busy();
}
void ContentJobs::Poll()
{
    if (!Busy() || !Impl_->State->Done.load(std::memory_order_acquire))
    {
        return;
    }
#if !defined(Q_OS_WASM)
    if (Impl_->Thread != nullptr && !Impl_->Thread->isFinished())
    {
        return;
    }
    delete Impl_->Thread;
    Impl_->Thread = nullptr;
#endif
    const auto result = Impl_->State->Result;
    Impl_->State.clear();
    PollTimer_.stop();
    Q_EMIT Completed(result);
    Q_EMIT BusyChanged();
}
} // namespace ludus::editor
