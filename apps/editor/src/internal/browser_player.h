#pragma once

#include <QString>

namespace ludus::editor
{
// Only shipped sample identifiers are admitted; project metadata cannot supply a URL.
QString BrowserPlayer(const QString& descriptor);
} // namespace ludus::editor
