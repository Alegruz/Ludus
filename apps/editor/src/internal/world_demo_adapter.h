#pragma once

#include "internal/scene_document.h"
#include "internal/world_document.h"

namespace ludus::editor
{
// World owns all durable authoring state. This adapter only converts the sample's
// legacy files and projects snapshots into its trusted renderer/player codec.
[[nodiscard]] bool IsWorldDemo(const WorldDocument& document) noexcept;
[[nodiscard]] bool ImportWorldDemo(WorldDocument& document, const QByteArray& source, QString& error);
[[nodiscard]] bool ProjectWorldDemo(const WorldDocument& document, world_demo::Level& output, QString& error);
[[nodiscard]] bool CommitWorldDemoGesture(WorldDocument& document, const SceneSnapshot& projection, QString& error);
} // namespace ludus::editor
