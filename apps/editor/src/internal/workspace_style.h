#pragma once

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace ludus::editor
{
// Private presentation helper; preserves native controls, fonts and palettes.
void ApplyWorkspaceBoundaries(QWidget* widget);
} // namespace ludus::editor
