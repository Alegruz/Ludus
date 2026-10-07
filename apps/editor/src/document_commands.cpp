#include "internal/main_window.h"

#include "internal/audio_workspace.h"
#include "internal/configuration_workspace.h"

#include <QAbstractItemDelegate>
#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTextDocument>

namespace ludus::editor
{
void MainWindow::CommitProjectFields()
{
    if (!Controller_->Caps().CanEdit)
    {
        return;
    }
    // A QListWidget delegate's text is not in the model until accepted. Save
    // and loss-of-work confirmation must include that pending argument buffer.
    auto* edit = qobject_cast<QLineEdit*>(QApplication::focusWidget());
    if (edit != nullptr && ArgsList_->isAncestorOf(edit) && ArgsList_->currentIndex().isValid())
    {
        ArgsList_->itemDelegate()->setModelData(edit, ArgsList_->model(), ArgsList_->currentIndex());
    }
    Controller_->EditDraft(DraftFromFields());
    LastEditedField_ = nullptr;
}

void MainWindow::RenderDocumentActions()
{
    if (SaveAction_ == nullptr || UndoAction_ == nullptr)
    {
        return;
    }
    auto* area = WorkTabs_->currentWidget();
    const bool project = area == ProjectSettings_;
    const bool audio = area != nullptr && area->isAncestorOf(Audio_);
    const bool configuration = area == Configuration_;
    SaveAction_->setText(project ? QStringLiteral("&Save Project Settings")
                         : audio ? QStringLiteral("&Save Audio")
                                 : QStringLiteral("&Save Preferences"));
    // Clean project saves are available too: the focused argument delegate may
    // contain pending text that has not yet made the document dirty.
    SaveAction_->setEnabled(project ? Controller_->Caps().CanEdit
                            : audio ? Audio_->CanSave()
                                    : configuration && Configuration_->Preview() != nullptr);
#if defined(Q_OS_WASM)
    if (project || configuration)
    {
        SaveAction_->setText(project ? QStringLiteral("Save and &Download Project")
                                     : QStringLiteral("Save and &Download Preferences"));
    }
#endif
    auto* focus = QApplication::focusWidget();
    auto* line = qobject_cast<QLineEdit*>(focus);
    auto* plain = qobject_cast<QPlainTextEdit*>(focus);
    if (line != nullptr)
    {
        UndoAction_->setEnabled(!line->isReadOnly() && line->isUndoAvailable());
        RedoAction_->setEnabled(!line->isReadOnly() && line->isRedoAvailable());
    }
    else if (plain != nullptr)
    {
        UndoAction_->setEnabled(!plain->isReadOnly() && plain->document()->isUndoAvailable());
        RedoAction_->setEnabled(!plain->isReadOnly() && plain->document()->isRedoAvailable());
    }
    else
    {
        const bool inProject = project && (focus == nullptr || ProjectSettings_->isAncestorOf(focus));
        UndoAction_->setEnabled(inProject && Controller_->CanUndoProject());
        RedoAction_->setEnabled(inProject && Controller_->CanRedoProject());
    }
}

void MainWindow::OnUndoRequested()
{
    if (auto* line = qobject_cast<QLineEdit*>(QApplication::focusWidget()))
    {
        line->undo();
    }
    else if (auto* plain = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget()))
    {
        plain->undo();
    }
    else if (WorkTabs_->currentWidget() == ProjectSettings_ && UndoAction_->isEnabled())
    {
        Controller_->UndoProjectEdit();
    }
    RenderDocumentActions();
}

void MainWindow::OnRedoRequested()
{
    if (auto* line = qobject_cast<QLineEdit*>(QApplication::focusWidget()))
    {
        line->redo();
    }
    else if (auto* plain = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget()))
    {
        plain->redo();
    }
    else if (WorkTabs_->currentWidget() == ProjectSettings_ && RedoAction_->isEnabled())
    {
        Controller_->RedoProjectEdit();
    }
    RenderDocumentActions();
}
} // namespace ludus::editor
