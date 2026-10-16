#include "internal/preview_viewport.h"

#include <ludus/foundation/math/scalar.hpp>
#include <ludus/foundation/math/vector.hpp>

#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QHideEvent>

namespace ludus::editor
{
using namespace foundation;
namespace math = foundation::math;
namespace
{
// Thanks to Epic Games, "Viewport Controls", Unreal Engine documentation:
// https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine
// Adopted RMB fly/look, Alt orbit and F framing; editor input remains outside GameplayCamera.
constexpr int32 FORWARD_KEY = 0;
constexpr int32 BACKWARD_KEY = 1;
constexpr int32 LEFT_KEY = 2;
constexpr int32 RIGHT_KEY = 3;
constexpr int32 DOWN_KEY = 4;
constexpr int32 UP_KEY = 5;
constexpr int32 BOOST_KEY = 6;
int32 GetMovementKey(int32 key) noexcept
{
    switch (key)
    {
        case Qt::Key_W:
            return FORWARD_KEY;
        case Qt::Key_S:
            return BACKWARD_KEY;
        case Qt::Key_A:
            return LEFT_KEY;
        case Qt::Key_D:
            return RIGHT_KEY;
        case Qt::Key_Q:
            return DOWN_KEY;
        case Qt::Key_E:
            return UP_KEY;
        case Qt::Key_Shift:
            return BOOST_KEY;
        default:
            return -1;
    }
}
} // namespace
void PreviewViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    resizeSurface();
}
void PreviewViewport::mousePressEvent(QMouseEvent* event)
{
    mLastPositionGlobal = event->globalPosition();
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::RightButton)
    {
        mIsFlying = true;
        mHeldKeys[BOOST_KEY] = event->modifiers().testFlag(Qt::ShiftModifier);
        mNavigationClock.start();
        mNavigationTimer.start();
        const QString platformName = QGuiApplication::platformName();
        if (isVisible() == true && platformName != QStringLiteral("offscreen") &&
            platformName != QStringLiteral("minimal") && platformName != QStringLiteral("wasm"))
        {
            grabMouse(Qt::BlankCursor);
            mHasMouseCapture = true;
            recenterPointer();
        }
    }
    event->accept();
}
void PreviewViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton)
    {
        stopNavigation();
    }
    event->accept();
}
void PreviewViewport::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF delta = event->globalPosition() - mLastPositionGlobal;
    mLastPositionGlobal = event->globalPosition();
    const float32 deltaX = static_cast<float32>(delta.x());
    const float32 deltaY = static_cast<float32>(delta.y());
    if (event->buttons().testFlag(Qt::MiddleButton) == true)
    {
        Preview_->Pan(deltaX, deltaY, getDrawableHeight());
    }
    else if (event->buttons().testFlag(Qt::LeftButton) == true && event->modifiers().testFlag(Qt::AltModifier) == true)
    {
        Preview_->Orbit(deltaX, deltaY);
    }
    else if (mIsFlying == true && event->buttons().testFlag(Qt::RightButton) == true)
    {
        Preview_->Look(deltaX, deltaY);
        recenterPointer();
    }
    else
    {
        event->ignore();
        return;
    }
    event->accept();
}
void PreviewViewport::wheelEvent(QWheelEvent* event)
{
    // Qt supplies logical pixel deltas for high-resolution trackpads; retain fractional notches otherwise.
    const float32 steps = event->pixelDelta().isNull() == false ? static_cast<float32>(event->pixelDelta().y()) / 80
                                                                : static_cast<float32>(event->angleDelta().y()) / 120;
    if (steps != 0)
    {
        if (mIsFlying == true)
        {
            mSpeedMultiplier =
                math::Clamp(mSpeedMultiplier * math::Clamp(1 + steps * 0.2F, 0.1F, 10.0F), 0.001F, 1000.0F);
        }
        else
        {
            Preview_->Zoom(steps);
        }
    }
    event->accept();
}
void PreviewViewport::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape)
    {
        stopNavigation();
        event->accept();
        return;
    }
    const int32 movementKey = GetMovementKey(event->key());
    if (mIsFlying == true && movementKey >= 0)
    {
        mHeldKeys[static_cast<usize>(movementKey)] = true;
        event->accept();
        return;
    }
    if (mIsFlying == false && event->modifiers() == Qt::NoModifier)
    {
        if (event->key() == Qt::Key_F)
        {
            static_cast<void>(Preview_->FrameSelection());
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Home)
        {
            Preview_->FrameAll();
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}
void PreviewViewport::keyReleaseEvent(QKeyEvent* event)
{
    const int32 movementKey = GetMovementKey(event->key());
    if (movementKey >= 0 && event->isAutoRepeat() == false)
    {
        mHeldKeys[static_cast<usize>(movementKey)] = false;
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}
void PreviewViewport::stopNavigation()
{
    mIsFlying = false;
    mNavigationTimer.stop();
    for (bool& isHeld : mHeldKeys)
    {
        isHeld = false;
    }
    if (mHasMouseCapture == true)
    {
        releaseMouse();
        mHasMouseCapture = false;
    }
}
void PreviewViewport::advanceNavigation()
{
    if (mIsFlying == false)
    {
        return;
    }
    const float32 deltaSeconds = static_cast<float32>(mNavigationClock.nsecsElapsed()) / 1000000000;
    mNavigationClock.restart();
    const math::Vector3 movementControls{
        (mHeldKeys[RIGHT_KEY] == true ? 1.0F : 0.0F) - (mHeldKeys[LEFT_KEY] == true ? 1.0F : 0.0F),
        (mHeldKeys[UP_KEY] == true ? 1.0F : 0.0F) - (mHeldKeys[DOWN_KEY] == true ? 1.0F : 0.0F),
        (mHeldKeys[BACKWARD_KEY] == true ? 1.0F : 0.0F) - (mHeldKeys[FORWARD_KEY] == true ? 1.0F : 0.0F)};
    const float32 speed = math::Clamp(Preview_->GetFocusDistance() * 0.5F, 0.001F, 10000.0F) * mSpeedMultiplier *
                          (mHeldKeys[BOOST_KEY] == true ? 4 : 1);
    Preview_->Fly(movementControls, deltaSeconds, speed);
}
void PreviewViewport::recenterPointer()
{
    if (mHasMouseCapture == true)
    {
        const QPoint centerGlobal = mapToGlobal(rect().center());
        mLastPositionGlobal = centerGlobal;
        QCursor::setPos(centerGlobal);
    }
}
void PreviewViewport::focusOutEvent(QFocusEvent* event)
{
    stopNavigation();
    QWidget::focusOutEvent(event);
}
void PreviewViewport::hideEvent(QHideEvent* event)
{
    stopNavigation();
    QWidget::hideEvent(event);
}
} // namespace ludus::editor
