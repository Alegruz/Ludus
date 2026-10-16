#pragma once

#include "internal/render_preview.h"

#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QWidget>

namespace ludus::editor
{
/// How the viewport shows engine frames in this process.
enum class ViewportPresentation : foundation::uint8
{
    /// Qt Quick and the engine share one GPU device: Qt's Metal device, or on Linux a Vulkan
    /// device the editor creates on Qt's instance and Qt Quick adopts (any Qt 6 release).
    SharedDevice,
    /// The engine owns a headless Vulkan device and the viewport paints double-buffered CPU copies.
    /// Used when forced with LUDUS_EDITOR_VIEWPORT=offscreen or when no shareable device presents.
    OffscreenReadback,
    /// Neither path exists on this platform; the viewport only explains why.
    Unavailable,
};
// One embedded presentation surface for both World bindings and cooked scenes.
class PreviewViewport final : public QWidget
{
public:
    explicit PreviewViewport(RenderPreview* preview, QWidget* parentOrNull = nullptr, bool isAutomatic = true);
    ~PreviewViewport() override;
    void ShowFrame();
    void ClearFrame(const QString& text);
    [[nodiscard]] foundation::uint64 PresentedFrames() const noexcept;
    /// True when either the shared-device or the offscreen readback path can show frames.
    [[nodiscard]] static bool CanPresent() noexcept;
    /// Select the presentation path from the Qt version and platform plugin. Setting the
    /// environment variable LUDUS_EDITOR_VIEWPORT=offscreen forces the readback path on Linux.
    [[nodiscard]] static ViewportPresentation GetPresentation() noexcept;

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
