#pragma once

#include "internal/render_preview.h"

#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QWidget>

namespace ludus::editor
{
// One embedded presentation surface for both World bindings and cooked scenes.
class PreviewViewport final : public QWidget
{
public:
    explicit PreviewViewport(RenderPreview* preview, QWidget* parentOrNull = nullptr, bool isAutomatic = true);
    ~PreviewViewport() override;
    void ShowFrame();
    void ClearFrame(const QString& text);
    [[nodiscard]] foundation::uint64 PresentedFrames() const noexcept;
    [[nodiscard]] static bool CanPresent() noexcept;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        resizeSurface();
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        Last_ = event->position().toPoint();
        setFocus();
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        const QPoint point = event->position().toPoint();
        const QPoint delta = point - Last_;
        Last_ = point;
        if (event->buttons().testFlag(Qt::MiddleButton) ||
            (event->buttons().testFlag(Qt::LeftButton) && event->modifiers().testFlag(Qt::ShiftModifier)))
        {
            Preview_->Pan(delta.x(), delta.y());
        }
        else if (event->buttons().testFlag(Qt::LeftButton) || event->buttons().testFlag(Qt::RightButton))
        {
            Preview_->Orbit(delta.x(), delta.y());
        }
        event->accept();
    }
    void wheelEvent(QWheelEvent* event) override
    {
        WheelRemainder_ += event->angleDelta().y();
        Preview_->Zoom(WheelRemainder_ / 120);
        WheelRemainder_ %= 120;
        event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        Preview_->FrameAll();
        event->accept();
    }

private:
    struct State;
    foundation::UniquePtr<State> mState;
    void resizeSurface();
    RenderPreview* Preview_;
    QPoint Last_;
    foundation::int32 WheelRemainder_ = 0;
};
} // namespace ludus::editor
