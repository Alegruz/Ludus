#pragma once

#include <ludus/foundation/base/types.h>

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
/// The central directory is validated and the declared expanded sizes are summed against
/// maximumExpandedBytes before anything is decompressed or written.
/// @param directory Existing or creatable destination directory.
/// @param maximumExpandedBytes Budget for the sum of expanded entry sizes (capped at 1 GiB).
/// @param outExpandedBytes Receives the expanded total on success.
/// @param outError Receives a reason on failure.
/// @return True when every entry was written.
[[nodiscard]] bool ExpandSceneArchive(const QByteArray& archive,
                                      const QString& directory,
                                      foundation::uint64 maximumExpandedBytes,
                                      foundation::uint64& outExpandedBytes,
                                      QString& outError);
} // namespace ludus::editor
