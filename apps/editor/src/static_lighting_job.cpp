#include "internal/static_lighting_job.h"

#include <ludus/foundation/base/pointer.hpp>
#include <new>

namespace ludus::editor
{
using namespace foundation;
namespace rr = graphics::renderer;
namespace rhi = graphics::rhi;
StaticLightingJob::StaticLightingJob(const RenderScene& scene, const rr::StaticLightingBakeDescription& description)
    : mScene(scene), mDescription(description)
{
    if (description.Surfaces.IsValid() == false)
    {
        mStatus = rhi::RasterStatus::InvalidDescription;
    }
    else if (description.Surfaces.IsEmpty() == false &&
             mSurfaces.TryAddRange({description.Surfaces.GetDataOrNull(), description.Surfaces.GetSize()}) == false)
    {
        mStatus = rhi::RasterStatus::OutOfMemory;
    }
    mDescription.Surfaces = {};
    if (mScene.Definition.isNull() == false)
    {
        for (const resources::SceneObject& object : mScene.Definition->Objects)
        {
            mTotalSamples += static_cast<uint64>(mScene.Definition->Meshes[object.Mesh].Indices.GetSize() / 3) *
                             mDescription.SamplesPerTriangle;
        }
    }
}
StaticLightingJob::~StaticLightingJob()
{
    requestInterruption();
    static_cast<void>(wait());
}
uint32 StaticLightingJob::GetProgressPercent() const noexcept
{
    const uint64 completed = mCompletedSamples.load(std::memory_order_relaxed);
    return mTotalSamples == 0 ? 0 : static_cast<uint32>(completed * 100 / mTotalSamples);
}
UniquePtr<rr::StaticLightingBake> StaticLightingJob::TakeBake() noexcept
{
    LUDUS_REQUIRE(isFinished() == true);
    return Move(mBake);
}
void StaticLightingJob::run()
{
    if (mStatus != rhi::RasterStatus::Pending)
    {
        return;
    }
    mDescription.Surfaces = {mSurfaces.GetData(), mSurfaces.GetSize()};
    UniquePtr<rr::StaticLightingBake> candidate;
    candidate.Reset(new (std::nothrow) rr::StaticLightingBake);
    if (candidate.Get() == nullptr || mScene.Definition.isNull() == true)
    {
        mStatus = rhi::RasterStatus::OutOfMemory;
        return;
    }
    mStatus = candidate->Initialize(*mScene.Definition, mDescription);
    constexpr uint32 PATHS_PER_STEP = 256;
    uint64 completed = 0;
    while (mStatus == rhi::RasterStatus::Pending && isInterruptionRequested() == false)
    {
        mStatus = candidate->Step(PATHS_PER_STEP);
        completed += PATHS_PER_STEP;
        mCompletedSamples.store(completed < mTotalSamples ? completed : mTotalSamples, std::memory_order_relaxed);
    }
    if (isInterruptionRequested() == true)
    {
        mStatus = rhi::RasterStatus::NotReady;
        return;
    }
    if (mStatus == rhi::RasterStatus::Ready)
    {
        mBake = Move(candidate);
    }
}
} // namespace ludus::editor
