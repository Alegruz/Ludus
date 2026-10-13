#pragma once

#include "internal/render_preview.h"

#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>

namespace ludus::editor
{
// One embedded presentation surface for both World bindings and cooked scenes.
class PreviewViewport final : public QLabel
{
public:
    explicit PreviewViewport(RenderPreview* preview, QWidget* parent = nullptr, bool automatic = true)
        : QLabel(parent), Preview_(preview)
    {
        setMinimumSize(160, 160);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        setAlignment(Qt::AlignCenter);
        setTextFormat(Qt::PlainText);
        setWordWrap(true);
        setFocusPolicy(Qt::StrongFocus);
        setText(QStringLiteral("Open a World to render it here."));
        setToolTip(
            QStringLiteral("Drag to orbit · middle/Shift-drag to pan · wheel to zoom · double-click to frame all"));
        if (automatic)
        {
            connect(preview, &RenderPreview::FrameReady, this, [this](const QImage& frame) {
                Frame_ = frame;
                Present();
            });
        }
    }
    void ShowFrame(const QImage& frame)
    {
        Frame_ = frame;
        Present();
    }
    void ClearFrame(const QString& text)
    {
        Frame_ = {};
        setText(text);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        const auto ratio = devicePixelRatioF();
        Preview_->Resize(qRound(static_cast<foundation::float64>(width()) * ratio),
                         qRound(static_cast<foundation::float64>(height()) * ratio));
        Present();
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        Last_ = event->position().toPoint();
        setFocus();
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        const auto point = event->position().toPoint();
        const auto delta = point - Last_;
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
    void Present()
    {
        if (!Frame_.isNull())
        {
            const auto ratio = devicePixelRatioF();
            auto pixmap = QPixmap::fromImage(Frame_).scaled(qRound(static_cast<foundation::float64>(width()) * ratio),
                                                            qRound(static_cast<foundation::float64>(height()) * ratio),
                                                            Qt::KeepAspectRatio,
                                                            Qt::SmoothTransformation);
            pixmap.setDevicePixelRatio(ratio);
            setPixmap(pixmap);
        }
    }
    RenderPreview* Preview_;
    QImage Frame_;
    QPoint Last_;
    foundation::int32 WheelRemainder_ = 0;
};
} // namespace ludus::editor
