#include "internal/scene_preview.h"
namespace ludus::editor
{
ScenePreview::ScenePreview(SceneDocument* document, QObject* parent) : QObject(parent), Document_(document) {}
ScenePreview::~ScenePreview()
{
    Stop();
}
bool ScenePreview::Start()
{
    Status_ = QStringLiteral("Authoring rendering is unavailable in this build. Enable LUDUS_EDITOR_RENDER_PREVIEW "
                             "with the pinned shader tools on desktop. The document remains editable and saveable.");
    Q_EMIT Changed();
    return false;
}
void ScenePreview::Stop() noexcept
{
    Document_->CancelTransform();
}
void ScenePreview::Tick() {}
void ScenePreview::Select(const world_demo::Name& id) noexcept
{
    Document_->CancelTransform();
    Selected_ = id;
}
platform::NativeWindowInfo ScenePreview::WindowInfo() const noexcept
{
    return {};
}
} // namespace ludus::editor
