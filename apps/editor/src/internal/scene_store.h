#pragma once

#include <ludus/foundation/base/types.h>

#include "internal/scene_document.h"
#include <QByteArray>
#include <QString>

namespace ludus::editor
{
// Qt host persistence only. Cooperative locks and QSaveFile match existing
// editor save guarantees; atomic visibility is not power-loss durability.
[[nodiscard]] bool ReadScene(const QString& path, QByteArray& output);
[[nodiscard]] bool EncodeScene(const SceneSnapshot& snapshot, QByteArray& output);
[[nodiscard]] bool
SaveScene(const QString& path, const QByteArray& baseline, SceneDocument& document, QByteArray& saved);
[[nodiscard]] bool WriteSceneRecovery(const QString& path, const QByteArray& baseline, const SceneDocument& document);
[[nodiscard]] bool ReadSceneRecovery(const QString& path, const QByteArray& baseline, SceneSnapshot& output);
} // namespace ludus::editor
