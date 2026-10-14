#pragma once

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/base/pointer.hpp>
#include <ludus/foundation/containers/array.hpp>
#include <ludus/graphics/renderer/static_lighting.hpp>

#include "internal/render_scene.h"

#include <QThread>
#include <atomic>

namespace ludus::editor
{
// Serial CPU preparation only. Immutable decoded storage stays alive through
// the existing RenderScene ownership boundary; no GPU or QWidget access here.
class StaticLightingJob final : public QThread
{
public:
    StaticLightingJob(const RenderScene& scene, const graphics::renderer::StaticLightingBakeDescription& description);
    ~StaticLightingJob() override;
    [[nodiscard]] foundation::uint32 GetProgressPercent() const noexcept;
    // Call only after wait(0) confirms completion and synchronizes all writes.
    [[nodiscard]] LUDUS_INLINE graphics::rhi::RasterStatus GetStatus() const noexcept
    {
        return mStatus;
    }
    [[nodiscard]] foundation::UniquePtr<graphics::renderer::StaticLightingBake> TakeBake() noexcept;

protected:
    void run() override;

private:
    RenderScene mScene;
    graphics::renderer::StaticLightingBakeDescription mDescription;
    foundation::UniquePtr<graphics::renderer::StaticLightingBake> mBake;
    foundation::Array<graphics::renderer::StaticLightingSurfaceDescription> mSurfaces;
    graphics::rhi::RasterStatus mStatus = graphics::rhi::RasterStatus::Pending;
    foundation::uint64 mTotalSamples = 0;
    std::atomic<foundation::uint64> mCompletedSamples{0};
};
} // namespace ludus::editor
