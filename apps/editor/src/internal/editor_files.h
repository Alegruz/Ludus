#pragma once

#include <QString>

#include <functional>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace ludus::editor
{
struct EditorDocumentDialog
{
    QString Title;
    QString Directory;
    QString Filter;
};

// Native dialogs retain the existing filesystem behavior. Browser imports copy
// bounded JSON into the session filesystem; completion is lifetime-guarded.
void OpenEditorDocument(QWidget* owner,
                        const EditorDocumentDialog& dialog,
                        const std::function<void(const QString&)>& loaded);
#if defined(Q_OS_WASM)
// Starts a browser download, which cannot confirm the user's destination/save.
[[nodiscard]] bool DownloadEditorDocument(QWidget* owner, const QString& path);
void MarkBrowserEdited();
#endif
} // namespace ludus::editor
