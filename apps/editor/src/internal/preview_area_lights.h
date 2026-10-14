#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/math/transform.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/graphics/renderer/lighting.hpp>
#include <ludus/runtime/resources/scene.h>

namespace ludus::editor
{
// Partition deindexed emitter geometry by shared geometric edges and coplanar
// outward normals. Append one centroid proxy per connected planar patch.
// Allocation, topology, identity or capacity failure preserves both outputs.
[[nodiscard]] graphics::rhi::RasterStatus AppendPreviewAreaLights(const resources::SceneMesh& mesh,
                                                                  const foundation::math::Affine3& meshToWorld,
                                                                  foundation::math::Vector3 resolvedEmission,
                                                                  bool isTwoSided,
                                                                  foundation::uint64 objectSourceId,
                                                                  foundation::float32 range,
                                                                  graphics::renderer::LightSelection& inoutLights,
                                                                  foundation::uint32& outAdded) noexcept;
} // namespace ludus::editor
