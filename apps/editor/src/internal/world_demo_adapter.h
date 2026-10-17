#pragma once

#include <ludus/foundation/identity/uuid.hpp>

#include "internal/scene_document.h"
#include "internal/world_document.h"

namespace ludus::editor
{
// World owns all durable authoring state. This adapter only converts the sample's
// legacy files and projects snapshots into its trusted renderer/player codec.
[[nodiscard]] bool IsWorldDemo(const WorldDocument& document) noexcept;
// A colliding settings-entity ID is replaced by one minted from `entropy`; when
// it is unavailable the import fails with `outError` and `inoutDocument` unchanged.
[[nodiscard]] bool ImportWorldDemo(WorldDocument& inoutDocument,
                                   const foundation::identity::EntropySource& entropy,
                                   const QByteArray& source,
                                   QString& outError);
[[nodiscard]] bool ProjectWorldDemo(const WorldDocument& document, world_demo::Level& output, QString& error);
[[nodiscard]] bool CommitWorldDemoGesture(WorldDocument& document, const SceneSnapshot& projection, QString& error);
} // namespace ludus::editor
