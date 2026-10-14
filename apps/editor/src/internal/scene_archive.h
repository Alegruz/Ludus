#pragma once

#include <QByteArray>
#include <QString>

namespace ludus::editor
{
/// Expand a .zip scene archive into a directory.
///
/// Used by the browser Editor to unpack a selected scene archive into MEMFS before cooking.
/// Supports stored and deflated entries without ZIP64 or encryption. Entry names that are
/// absolute, contain "..", backslashes or drive letters are rejected so nothing escapes the
/// directory. Directory entries are created implicitly.
/// @param archive Complete archive bytes.
/// @param directory Existing or creatable destination directory.
/// @param outError Receives a reason on failure.
/// @return True when every entry was written.
[[nodiscard]] bool ExpandSceneArchive(const QByteArray& archive, const QString& directory, QString& outError);
} // namespace ludus::editor
