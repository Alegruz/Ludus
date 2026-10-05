#include "internal/workspace_style.h"

#include <QColor>
#include <QPalette>
#include <QString>
#include <QWidget>

namespace ludus::editor
{
void ApplyWorkspaceBoundaries(QWidget* widget)
{
    // Thanks to David Lightbown, Designing the User Experience of Game
    // Development Tools (CRC Press, 2015), ch. 5, pp. 80-86: expose hierarchy
    // and preserve familiar controls. This adds boundaries, not a widget skin.
    // See docs/architecture/editor-design-review.md and
    // https://www.uxofgametools.com/ (2015 edition consulted).
    const auto border = widget->palette().color(QPalette::Window).lightness() < 128 ? QStringLiteral("#65717d")
                                                                                    : QStringLiteral("#7b8794");
    widget->setStyleSheet(QStringLiteral("QDialog { border: 2px solid %1; } "
                                         "QDockWidget::title { border: 1px solid %1; padding: 6px; } "
                                         "QTabWidget::pane { border: 1px solid %1; } "
                                         "QMainWindow::separator { background: %1; width: 4px; height: 4px; }")
                              .arg(border));
}
} // namespace ludus::editor
