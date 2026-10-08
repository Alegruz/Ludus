#pragma once

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace ludus::editor
{
// Private presentation helper; shared light/dark brand roles; retains platform fonts and decoration.
void ApplyWorkspaceStyle(QWidget* widget);
} // namespace ludus::editor
