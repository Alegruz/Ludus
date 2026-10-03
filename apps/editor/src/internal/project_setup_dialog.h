#pragma once

#include <ludus/foundation/base/types.h>

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QWidget;

namespace ludus::editor
{
struct ProjectSetupOptions
{
    QString Sdk;
    QString WebSdk;
    bool PrepareEngine = false;
    bool DisableWeb = false;
};

class ProjectSetupDialog final : public QDialog
{
public:
    explicit ProjectSetupDialog(const QString& projectDirectory, QWidget* parent = nullptr);
    [[nodiscard]] ProjectSetupOptions Options() const noexcept;

private:
    void Refresh() noexcept;

    enum class EngineChoice : foundation::uint8
    {
        Current,
        Build,
        Installed,
    };
    QComboBox* Engine_ = nullptr;
    QLabel* EngineHelp_ = nullptr;
    QWidget* NativeFields_ = nullptr;
    QLineEdit* Sdk_ = nullptr;
    QCheckBox* Browser_ = nullptr;
    QWidget* BrowserFields_ = nullptr;
    QLineEdit* WebSdk_ = nullptr;
    QDialogButtonBox* Buttons_ = nullptr;
};
} // namespace ludus::editor
