// Counts ordinary, aligned and nothrow C++ allocations on production evaluation/view paths.
#include "internal/camera_view.h"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdlib>
#include <ludus/gameplay/camera/evaluation.hpp>
#include <new>
namespace
{
long gNews = 0;
long gDeletes = 0;
bool gTracking = false;

void* AlignedAlloc(std::size_t n, std::size_t al) noexcept
{
    if (al < alignof(std::max_align_t))
    {
        al = alignof(std::max_align_t);
    }
    std::size_t rounded = (n + al - 1) & ~(al - 1);
    if (rounded == 0)
    {
        rounded = al;
    }
    return std::aligned_alloc(al, rounded);
}
void CountNew() noexcept
{
    if (gTracking)
    {
        ++gNews;
    }
}
void CountDelete(void* p) noexcept
{
    if (p != nullptr && gTracking)
    {
        ++gDeletes;
    }
}
} // namespace

void* operator new(std::size_t n)
{
    CountNew();
    void* p = AlignedAlloc(n, alignof(std::max_align_t));
    if (p == nullptr)
    {
        throw std::bad_alloc{};
    }
    return p;
}
void* operator new(std::size_t n, std::align_val_t al)
{
    CountNew();
    void* p = AlignedAlloc(n, static_cast<std::size_t>(al));
    if (p == nullptr)
    {
        throw std::bad_alloc{};
    }
    return p;
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept
{
    CountNew();
    return AlignedAlloc(n, alignof(std::max_align_t));
}
void operator delete(void* p) noexcept
{
    CountDelete(p);
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    CountDelete(p);
    std::free(p);
}
void operator delete(void* p, std::align_val_t) noexcept
{
    CountDelete(p);
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    CountDelete(p);
    std::free(p);
}

TEST_CASE("Camera evaluation traces and renderer extraction allocate nothing", "[camera][allocation]")
{
    using namespace ludus::gameplay::camera;
    using namespace ludus::world_demo;
    const CameraRigDefinition fixed;
    const CameraRigDefinition follow{ .Recipe = CameraRecipe::OrthographicFollow };
    CameraTargetSample target;
    CameraSample sample;
    CameraTrace trace;
    CameraRenderView view;
    PlanarCameraView planar;
    bool passed = true;
    gNews = 0;
    gTracking = true;
    for (ludus::foundation::uint64 frame = 0; frame < 10000; ++frame)
    {
        target.Position.X = static_cast<ludus::foundation::float64>(frame) / 100;
        passed &= TryEvaluateCamera(frame % 2 == 0 ? fixed : follow,
                                    { .PresentationSequence = frame },
                                    &target,
                                    sample,
                                    &trace) == CameraStatus::Success;
        passed &= TryBuildCameraView(sample, {0, 0, 800, 600}, {}, 1e6, view) == CameraStatus::Success;
        passed &= TryBuildPlanarCameraView(sample, {0, 0, 480, 740}, {}, 1e6, planar) == CameraStatus::Success;
        passed &= TryEvaluateCamera(follow, {}, nullptr, sample, &trace) == CameraStatus::MissingTarget;
    }
    gTracking = false;
    REQUIRE(passed);
    REQUIRE(gNews == 0);
}
