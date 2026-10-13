#include "renderer_authored.h"
#include "renderer_authored_shadow.h"
#include "renderer_composite.h"
#include "renderer_flat.h"
#include "renderer_output.h"
#include "renderer_shadow.h"

#include <ludus/foundation/math/vector.hpp>
#include <ludus/foundation/time/time.hpp>
#include <ludus/graphics/renderer/environment.hpp>
#include <ludus/graphics/renderer/renderer.hpp>
#include <ludus/graphics/renderer/views.hpp>
#include <ludus/graphics/rhi/device.h>

using namespace ludus::foundation;
namespace rr = ludus::graphics::renderer;
namespace rhi = ludus::graphics::rhi;
namespace shaders = ludus::shaders;
int32 main()
{
    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    rhi::DeviceDescription request;
    request.Required.PortableRaster = true;
    request.RequiredProfile.Raster.MinBindings = 10;
    if (rhi::CreateDevice({}, { .Width = 8, .Height = 8 }, request, device, surface) != rhi::DeviceStatus::Ready)
    {
        return 1;
    }
    struct Guard final
    {
        ~Guard()
        {
            rhi::Shutdown();
        }
    } guard;
    rr::Renderer renderer;
    if (renderer.Initialize(device, shaders::renderer_flat::Vertex(), shaders::renderer_flat::Fragment()) !=
            rhi::RasterStatus::Ready ||
        renderer.InitializeViews(shaders::renderer_composite::Vertex(), shaders::renderer_composite::Fragment()) !=
            rhi::RasterStatus::Ready ||
        renderer.InitializeLighting(shaders::renderer_output::Vertex(),
                                    shaders::renderer_output::Fragment(),
                                    shaders::renderer_shadow::Fragment()) != rhi::RasterStatus::Ready)
    {
        return 2;
    }
    rr::MaterialProgram program;
    rhi::RasterStatus status = renderer.PrewarmLitProgram(shaders::renderer_authored::Vertex(),
                                                          shaders::renderer_authored::Fragment(),
                                                          shaders::renderer_authored_shadow::Fragment(),
                                                          program);
    const uint64 start = time::NowTicks();
    while (status == rhi::RasterStatus::Pending && time::NowTicks() - start < 10 * time::NANOSECONDS_PER_SECOND)
    {
        status = renderer.GetStatus(program);
    }
    if (status != rhi::RasterStatus::Ready)
    {
        return 3;
    }
    const math::Vector4 pixels[]{{125952, 4, 1, 1}, {0, 2, 0, 1}, {1, 0, 0, 1}, {0, 0, 0, 1}};
    rr::EnvironmentDescription source;
    source.PixelsOrNull = pixels;
    source.Width = source.Height = 2;
    source.FilterWidth = 16;
    source.FilterSamples = 16;
    source.CpuByteBudget = usize{1024} * 1024;
    rr::EnvironmentMap environment;
    status = environment.Initialize(device, source);
    const uint64 environmentStart = time::NowTicks();
    while (status == rhi::RasterStatus::Pending &&
           time::NowTicks() - environmentStart < 10 * time::NANOSECONDS_PER_SECOND)
    {
        status = environment.GetStatus();
    }
    if (status != rhi::RasterStatus::Ready)
    {
        return 4;
    }
    rr::LitMaterialDescription description;
    description.AreAuthoredInputsEnabled = true;
    description.EnvironmentOrNull = &environment;
    rr::MaterialVersion material;
    rr::LightSelection lights;
    status = renderer.CreateLitMaterial(program, description, material, lights);
    while (status == rhi::RasterStatus::Pending && time::NowTicks() - start < 10 * time::NANOSECONDS_PER_SECOND)
    {
        status = renderer.GetStatus(material);
    }
    if (status != rhi::RasterStatus::Ready)
    {
        return 5;
    }
    environment.Reset();
    if (renderer.GetStatus(material) != rhi::RasterStatus::Ready)
    {
        return 6;
    }
    return renderer.Release(material) == rhi::RasterStatus::Ready ? 0 : 7;
}
