#include "internal/editor_files.h"

#include <ludus/foundation/base/types.h>

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QSaveFile>
#include <QSharedPointer>
#include <QWeakPointer>
#include <QWidget>

#include <utility>

#if defined(Q_OS_WASM)
#    include <QtCore/private/qstdweb_p.h>
#endif

namespace ludus::editor
{
#if defined(Q_OS_WASM)
namespace
{
constexpr foundation::int64 MAX_DOCUMENT_BYTES = 1024LL * 1024;
constexpr foundation::int64 MAX_IMPORT_BYTES = 32 * MAX_DOCUMENT_BYTES;
foundation::int64 ImportedBytes = 0;
foundation::uint32 ImportSerial = 0;

struct BrowserImport
{
    QPointer<QWidget> Owner;
    QString Name;
    emscripten::val Input = emscripten::val::undefined();
    QSharedPointer<qstdweb::EventCallback> Change;
    QSharedPointer<qstdweb::EventCallback> Cancel;
    QSharedPointer<qstdweb::FileReader> Reader;
    foundation::uint64 Size = 0;
    std::function<void(const QString&)> Completed;
};
QSharedPointer<BrowserImport> PendingImport;
void Report(QWidget* owner, const QString& message)
{
    auto* dialog =
        new QMessageBox(QMessageBox::Warning, QStringLiteral("Browser files"), message, QMessageBox::Ok, owner);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}
} // namespace
extern "C" void LudusBrowserDownloadDocument(const char* name, const foundation::uint8* bytes, foundation::int32 size);
extern "C" void LudusBrowserMarkEdited();
void MarkBrowserEdited()
{
    LudusBrowserMarkEdited();
}
#endif

void OpenEditorDocument(QWidget* owner, const EditorDocumentDialog& dialog, std::function<void(const QString&)> loaded)
{
#if defined(Q_OS_WASM)
    (void)dialog;
    if (PendingImport)
    {
        return;
    }
    auto state = QSharedPointer<BrowserImport>::create();
    PendingImport = state;
    state->Owner = owner;
    state->Completed = std::move(loaded);
    const QWeakPointer<BrowserImport> weak(state);
    // Thanks to Qt Group, qstdweb::EventCallback/FileReader (Qt 6.10.3): these
    // callbacks participate in Qt's suspend/resume dispatcher. A raw Wasm call
    // from a JavaScript promise cannot safely re-enter modal editor workflows.
    // Thanks also to MDN contributors, "<input type=file>" and "cancel event":
    // check size before reading, and release selection on cancellation.
    // https://developer.mozilla.org/en-US/docs/Web/HTML/Reference/Elements/input/file
    // https://developer.mozilla.org/en-US/docs/Web/API/HTMLInputElement/cancel_event
    // Only this private adapter depends on the exact pinned Qt implementation.
    // https://code.qt.io/cgit/qt/qtbase.git/tree/src/corelib/platform/wasm/qstdweb.cpp?h=v6.10.3
    state->Input = emscripten::val::global("document").call<emscripten::val>("createElement", emscripten::val("input"));
    state->Input.set("type", emscripten::val("file"));
    state->Input.set("accept", emscripten::val(".json,application/json"));
    state->Input.set("hidden", true);
    auto finish = [weak]() {
        const auto state = weak.toStrongRef();
        if (state)
        {
            state->Input.call<void>("remove");
            if (PendingImport == state)
            {
                PendingImport.clear();
            }
        }
    };
    state->Cancel = QSharedPointer<qstdweb::EventCallback>::create(state->Input,
                                                                   "cancel",
                                                                   [finish](const emscripten::val&) { finish(); });
    state->Change =
        QSharedPointer<qstdweb::EventCallback>::create(state->Input, "change", [weak, finish](const emscripten::val&) {
            const auto state = weak.toStrongRef();
            if (!state || state->Owner.isNull())
            {
                finish();
                return;
            }
            const auto files = state->Input["files"];
            if (files["length"].as<foundation::uint32>() != 1)
            {
                finish();
                return;
            }
            const qstdweb::File file(files[0]);
            state->Size = file.size();
            if (state->Size == 0 || state->Size > static_cast<foundation::uint64>(MAX_DOCUMENT_BYTES) ||
                state->Size > static_cast<foundation::uint64>(MAX_IMPORT_BYTES - ImportedBytes))
            {
                Report(
                    state->Owner,
                    QStringLiteral(
                        "Choose a nonempty JSON document up to 1 MiB. This session admits up to 32 MiB of imports."));
                finish();
                return;
            }
            state->Name = QFileInfo(QString::fromStdString(file.name())).fileName();
            if (state->Name.isEmpty() || state->Name == QStringLiteral(".") || state->Name == QStringLiteral(".."))
            {
                Report(state->Owner, QStringLiteral("The selected document has no usable filename."));
                finish();
                return;
            }
            ImportedBytes += static_cast<foundation::int64>(state->Size);
            state->Reader = QSharedPointer<qstdweb::FileReader>::create();
            auto failed = [weak, finish](const emscripten::val&) {
                const auto state = weak.toStrongRef();
                if (state && !state->Owner.isNull())
                {
                    Report(state->Owner, QStringLiteral("Import failed; the current document was retained."));
                }
                finish();
            };
            state->Reader->onError(failed);
            state->Reader->onAbort(failed);
            state->Reader->onLoad([weak, finish](const emscripten::val&) {
                const auto state = weak.toStrongRef();
                if (!state || state->Owner.isNull())
                {
                    finish();
                    return;
                }
                const auto result = state->Reader->result();
                if (result.byteLength() != state->Size)
                {
                    Report(state->Owner, QStringLiteral("Could not read the complete selected document."));
                    finish();
                    return;
                }
                const auto bytes = qstdweb::Uint8Array(result).copyToQByteArray();
                const auto directory = QStringLiteral("/browser-imports/") + QString::number(++ImportSerial);
                const auto path = QDir(directory).filePath(state->Name);
                QSaveFile output(path);
                output.setDirectWriteFallback(false);
                if (!QDir().mkpath(directory) || !output.open(QIODevice::WriteOnly) ||
                    output.write(bytes) != bytes.size() || !output.commit())
                {
                    Report(state->Owner, QStringLiteral("Import failed; the current document was retained."));
                    finish();
                    return;
                }
                finish();
                state->Completed(path);
            });
            state->Reader->readAsArrayBuffer(file.slice(0, state->Size));
        });
    emscripten::val::global("document")["body"].call<void>("appendChild", state->Input);
    state->Input.call<void>("click");
#else
    const auto path = QFileDialog::getOpenFileName(owner, dialog.Title, dialog.Directory, dialog.Filter);
    if (!path.isEmpty())
    {
        loaded(path);
    }
#endif
}

#if defined(Q_OS_WASM)
bool DownloadEditorDocument(QWidget* owner, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > MAX_DOCUMENT_BYTES)
    {
        Report(owner, QStringLiteral("Could not read the document for download."));
        return false;
    }
    const auto bytes = file.readAll();
    if (bytes.size() != file.size())
    {
        Report(owner, QStringLiteral("Could not read the complete document for download."));
        return false;
    }
    const auto name = QFileInfo(path).fileName().toUtf8();
    LudusBrowserDownloadDocument(name.constData(),
                                 reinterpret_cast<const foundation::uint8*>(bytes.constData()),
                                 static_cast<foundation::int32>(bytes.size()));
    return true;
}
#endif
} // namespace ludus::editor
