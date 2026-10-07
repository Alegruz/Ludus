#pragma once

#include <ludus/foundation/config/config.hpp>

#include <QByteArray>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QTableWidget;
QT_END_NAMESPACE

namespace ludus::editor
{
// Private offline authoring workspace. It edits the runtime-owned host schema,
// never accesses a running host, and saves only explicit preference overrides.
class ConfigurationWorkspace final : public QWidget
{
public:
    explicit ConfigurationWorkspace(QWidget* parent = nullptr);
    ~ConfigurationWorkspace() override;
    [[nodiscard]] bool Load(const QString& path, foundation::config::Layer layer);
    [[nodiscard]] bool SavePreferences(const QString& path);
    // Save the applied preference draft; the separate Apply command owns the
    // value-entry buffer. Reuse the last path, or request one on the first save.
    void Save(bool choosePath = false);
    [[nodiscard]] bool Edit(std::string_view name, const QString& text, bool reset = false);
    [[nodiscard]] bool ConfirmDiscard();
    [[nodiscard]] bool Dirty() const noexcept
    {
        return Dirty_;
    }
    [[nodiscard]] const foundation::config::Context* Preview() const noexcept
    {
        return Context_;
    }

private:
    void Render();
    bool Report(foundation::config::Status status, const foundation::config::Diagnostic& error = {});
    foundation::config::Context* Context_ = nullptr;
    QTableWidget* Table_ = nullptr;
    QLineEdit* Value_ = nullptr;
    QLabel* Status_ = nullptr;
    QString PreferencePath_;
    QByteArray PreferenceStamp_;
    bool Dirty_ = false;
};
} // namespace ludus::editor
