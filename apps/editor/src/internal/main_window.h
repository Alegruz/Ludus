#pragma once

// MainWindow: renders controller state and emits actions. It owns no second
// settings model and starts no processes (design.md sections 3, 5).
//
// Private editor header (not installed). Uses standard Qt layouts/controls only.
// While synchronizing fields from state it blocks edit signals (QSignalBlocker)
// so rendering never dispatches new edits; reentrant actions are deferred to the
// next event-loop turn.

#include "internal/controller.h"

#include <QMainWindow>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QAction;
QT_END_NAMESPACE

namespace ludus::editor
{

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(EditorController* controller, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void OnStateChanged();
    void OnOpenRequested();
    void OnSaveRequested();
    void OnReloadRequested();
    void OnFieldEdited();
    void OnAddArgument();
    void OnRemoveArgument();
    void OnCopyJobDetails();

private:
    void BuildUi();
    void BuildMenus();
    [[nodiscard]] ProjectDescriptor DraftFromFields() const;
    void RenderFields();
    void RenderCapabilities();
    void RenderStatus();

    EditorController* Controller_ = nullptr;
    bool Rendering_ = false;
    bool CloseConfirmed_ = false;

    // Settings form controls.
    QLineEdit* NameEdit_ = nullptr;
    QComboBox* ProviderBox_ = nullptr;
    QLineEdit* SourceDirEdit_ = nullptr;
    QComboBox* PresetBox_ = nullptr;
    QComboBox* TargetBox_ = nullptr; // editable; executable dropdown after Configure
    QLineEdit* CwdEdit_ = nullptr;
    QListWidget* ArgsList_ = nullptr;

    // Actions / status.
    QAction* OpenAction_ = nullptr;
    QAction* SaveAction_ = nullptr;
    QAction* ReloadAction_ = nullptr;
    QAction* ConfigureAction_ = nullptr;
    QAction* BuildAction_ = nullptr;
    QAction* BuildRunAction_ = nullptr;
    QAction* StopAction_ = nullptr;
    QAction* ClearAction_ = nullptr;
    QAction* CopyAction_ = nullptr;
    QPushButton* AddArgButton_ = nullptr;
    QPushButton* RemoveArgButton_ = nullptr;
    QLabel* StatusLabel_ = nullptr;
    QLabel* RuntimeLabel_ = nullptr;
    QPlainTextEdit* Output_ = nullptr;
};

} // namespace ludus::editor
