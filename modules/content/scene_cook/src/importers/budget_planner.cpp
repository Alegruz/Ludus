#include "budget_planner.h"

#include "pbrt/transform_utilities.h"

#include <ludus/content/scene_format/luscn.h>
#include <ludus/foundation/containers/sort.hpp>
#include <ludus/foundation/math/scalar.hpp>

namespace ludus::content::scene_cook::importers
{
namespace
{
namespace format = content::scene_format;

[[nodiscard]] bool IsCancelled(const Cancellation* cancelOrNull) noexcept
{
    return cancelOrNull != nullptr && cancelOrNull->Requested != nullptr &&
           cancelOrNull->Requested(cancelOrNull->Context) == true;
}

[[nodiscard]] bool IsEmissive(const IrShape& shape) noexcept
{
    return shape.Emission[0] > 0.0 || shape.Emission[1] > 0.0 || shape.Emission[2] > 0.0;
}

[[nodiscard]] float64 ComputeLuminance(const float64 (&colour)[3]) noexcept
{
    return .2126 * colour[0] + .7152 * colour[1] + .0722 * colour[2];
}

[[nodiscard]] float64 ComputeWorldArea(const IrMesh& mesh, const math::Matrix4d& transform) noexcept
{
    float64 area = 0.0;
    const auto load = [&](uint32 index) noexcept {
        const float32* p = mesh.Positions.GetData() + usize{index} * 3;
        return TransformPoint(
            transform,
            math::Vector3d{static_cast<float64>(p[0]), static_cast<float64>(p[1]), static_cast<float64>(p[2])});
    };
    for (usize i = 0; i + 2 < mesh.Indices.GetSize(); i += 3)
    {
        const math::Vector3d a = load(mesh.Indices[i]);
        const math::Vector3d b = load(mesh.Indices[i + 1]);
        const math::Vector3d c = load(mesh.Indices[i + 2]);
        area += .5 * math::Length(math::Cross(b - a, c - a));
    }
    return math::IsFinite(area) == true ? area : 0.0;
}

/// Material terms of an IR index (IR_NONE is pbrt's default diffuse 0.5).
struct MaterialTerms final
{
    float64 BaseColour[3] = {.5, .5, .5};
    uint32 Texture = IR_NONE;
    float64 Metallic = 0.0;
    float64 Roughness = 1.0;
};

[[nodiscard]] MaterialTerms GetTerms(const SceneIr& scene, uint32 material) noexcept
{
    MaterialTerms terms;
    if (material == IR_NONE || material >= scene.Materials.GetSize())
    {
        return terms;
    }
    const IrMaterial& source = scene.Materials[material];
    for (usize k = 0; k < 3; ++k)
    {
        terms.BaseColour[k] = source.BaseColour[k];
    }
    terms.Texture = source.BaseColourTexture;
    terms.Metallic = source.Metallic;
    terms.Roughness = source.Roughness;
    return terms;
}

[[nodiscard]] float64 MeasureDistance(const MaterialTerms& left, const MaterialTerms& right) noexcept
{
    float64 distance = 0.0;
    for (usize k = 0; k < 3; ++k)
    {
        const float64 delta = left.BaseColour[k] - right.BaseColour[k];
        distance += delta * delta;
    }
    distance += (left.Metallic - right.Metallic) * (left.Metallic - right.Metallic);
    distance += .25 * (left.Roughness - right.Roughness) * (left.Roughness - right.Roughness);
    if (left.Texture != right.Texture)
    {
        distance += .1;
    }
    return distance;
}

struct MaterialUse final
{
    /// IR material index or IR_NONE.
    uint32 Material = IR_NONE;
    float64 Area = 0.0;
    uint32 Slot = IR_NONE;
};
} // namespace

Status
PlanBudget(const SceneIr& scene, DiagnosticLog& inoutLog, CookPlan& outPlan, const Cancellation* cancelOrNull) noexcept
{
    CookPlan plan;
    // 1. Expand placements.
    const auto addPlacement = [&](uint32 shape, const math::Matrix4d& transform, bool isInstanced) noexcept -> bool {
        Placement placement;
        placement.Shape = shape;
        placement.Transform = transform;
        placement.IsInstanced = isInstanced;
        return plan.Placements.TryAdd(placement);
    };
    for (const uint32 shape : scene.WorldShapes)
    {
        if (addPlacement(shape, scene.Shapes[shape].Transform, false) == false)
        {
            return Status::OutOfMemory;
        }
    }
    for (const IrInstance& instance : scene.Instances)
    {
        for (const uint32 shape : scene.Objects[instance.Object].Shapes)
        {
            if (addPlacement(shape, MultiplyMatrices(instance.Transform, scene.Shapes[shape].Transform), true) == false)
            {
                return Status::OutOfMemory;
            }
        }
    }
    if (plan.Placements.IsEmpty() == true)
    {
        (void)inoutLog.Add(DiagnosticSeverity::Error, codes::LIMIT_EXCEEDED, {}, 0, "the scene has no drawable shapes");
        return Status::Invalid;
    }
    uint64 sourceVertices = 0;
    for (Placement& placement : plan.Placements)
    {
        if (IsCancelled(cancelOrNull) == true)
        {
            return Status::Cancelled;
        }
        const IrMesh& mesh = scene.Meshes[scene.Shapes[placement.Shape].Mesh];
        placement.Area = ComputeWorldArea(mesh, placement.Transform);
        sourceVertices += mesh.GetVertexCount();
    }
    plan.SourceVertices = sourceVertices > 0xffffffffULL ? 0xffffffffU : static_cast<uint32>(sourceVertices);

    // 2. Rank placements by area (ties by order) and keep them within the vertex budget.
    Array<uint32> order;
    if (order.TryResize(plan.Placements.GetSize()) == false)
    {
        return Status::OutOfMemory;
    }
    for (usize i = 0; i < order.GetSize(); ++i)
    {
        order[i] = static_cast<uint32>(i);
    }
    foundation::Sort(order.GetData(), order.GetSize(), [&](uint32 left, uint32 right) noexcept {
        const float64 a = plan.Placements[left].Area;
        const float64 b = plan.Placements[right].Area;
        return a > b || (a == b && left < right);
    });

    // 3. Material usage by area (emissive surfaces are tracked separately).
    Array<MaterialUse> uses;
    float64 emissiveArea = 0.0;
    float64 brightestEmission[3] = {0.0, 0.0, 0.0};
    bool hasEmissive = false;
    for (const Placement& placement : plan.Placements)
    {
        const IrShape& shape = scene.Shapes[placement.Shape];
        if (IsEmissive(shape) == true)
        {
            hasEmissive = true;
            emissiveArea += placement.Area;
            if (ComputeLuminance(shape.Emission) > ComputeLuminance(brightestEmission))
            {
                for (usize k = 0; k < 3; ++k)
                {
                    brightestEmission[k] = shape.Emission[k];
                }
            }
            continue;
        }
        MaterialUse* use = nullptr;
        for (MaterialUse& existing : uses)
        {
            if (existing.Material == shape.Material)
            {
                use = &existing;
                break;
            }
        }
        if (use == nullptr)
        {
            use = uses.TryAddInPlace();
            if (use == nullptr)
            {
                return Status::OutOfMemory;
            }
            use->Material = shape.Material;
        }
        use->Area += placement.Area;
    }
    Array<uint32> useOrder;
    if (useOrder.TryResize(uses.GetSize()) == false)
    {
        return Status::OutOfMemory;
    }
    for (usize i = 0; i < useOrder.GetSize(); ++i)
    {
        useOrder[i] = static_cast<uint32>(i);
    }
    foundation::Sort(useOrder.GetData(), useOrder.GetSize(), [&](uint32 left, uint32 right) noexcept {
        const float64 a = uses[left].Area;
        const float64 b = uses[right].Area;
        return a > b || (a == b && left < right);
    });
    // Each cooked mesh carries exactly one material, so the slot count is bounded by MAX_MESHES.
    const usize slotLimit = format::MAX_MESHES < format::MAX_MATERIALS ? format::MAX_MESHES : format::MAX_MATERIALS;
    const usize keptLimit = hasEmissive == true ? slotLimit - 1 : slotLimit;
    const usize keptCount = useOrder.GetSize() < keptLimit ? useOrder.GetSize() : keptLimit;
    for (usize rank = 0; rank < keptCount; ++rank)
    {
        MaterialUse& use = uses[useOrder[rank]];
        PlannedMaterial* planned = plan.Materials.TryAddInPlace();
        if (planned == nullptr)
        {
            return Status::OutOfMemory;
        }
        const MaterialTerms terms = GetTerms(scene, use.Material);
        planned->Representative = use.Material;
        for (usize k = 0; k < 3; ++k)
        {
            planned->BaseColour[k] = terms.BaseColour[k];
        }
        planned->Texture = terms.Texture;
        planned->IsTextureRepeated = terms.Texture == IR_NONE || scene.Textures[terms.Texture].IsRepeated == true;
        planned->Metallic = math::Clamp(terms.Metallic, 0.0, 1.0);
        planned->Roughness = math::Clamp(terms.Roughness, static_cast<float64>(format::MIN_ROUGHNESS), 1.0);
        use.Slot = static_cast<uint32>(rank);
    }
    uint32 mergedCount = 0;
    for (usize rank = keptCount; rank < useOrder.GetSize(); ++rank)
    {
        MaterialUse& use = uses[useOrder[rank]];
        const MaterialTerms terms = GetTerms(scene, use.Material);
        float64 best = 0.0;
        for (usize slot = 0; slot < keptCount; ++slot)
        {
            const float64 distance = MeasureDistance(terms, GetTerms(scene, plan.Materials[slot].Representative));
            if (use.Slot == IR_NONE || distance < best)
            {
                best = distance;
                use.Slot = static_cast<uint32>(slot);
            }
        }
        ++mergedCount;
    }
    for (const MaterialUse& use : uses)
    {
        if (plan.Materials[use.Slot].Sources.TryAdd(use.Material) == false)
        {
            return Status::OutOfMemory;
        }
    }
    uint32 emissiveSlot = IR_NONE;
    if (hasEmissive == true)
    {
        emissiveSlot = static_cast<uint32>(plan.Materials.GetSize());
        PlannedMaterial* planned = plan.Materials.TryAddInPlace();
        if (planned == nullptr)
        {
            return Status::OutOfMemory;
        }
        planned->IsEmissive = true;
        planned->BaseColour[0] = planned->BaseColour[1] = planned->BaseColour[2] = 1.0;
        planned->IsTextureRepeated = false;
        planned->Area = emissiveArea;
        bool isClamped = false;
        for (usize k = 0; k < 3; ++k)
        {
            planned->Emission[k] = math::Min(brightestEmission[k], static_cast<float64>(format::MAX_EMISSION));
            isClamped = isClamped == true || brightestEmission[k] > static_cast<float64>(format::MAX_EMISSION);
        }
        if (isClamped == true)
        {
            (void)inoutLog.Add(DiagnosticSeverity::Warning,
                               codes::EMISSION_CLAMPED,
                               {},
                               0,
                               "emission clamped to the LUSCN maximum");
        }
        if (scene.Lights.Area > 1)
        {
            (void)inoutLog.Add(DiagnosticSeverity::Info,
                               codes::MATERIALS_MERGED,
                               {},
                               0,
                               "area lights merged into one emissive slot (brightest radiance kept)");
        }
    }
    if (mergedCount != 0)
    {
        char text[96]{};
        usize length = 0;
        const auto append = [&](std::string_view part) noexcept {
            for (usize i = 0; i < part.size() && length < sizeof(text); ++i)
            {
                text[length++] = part[i];
            }
        };
        char digits[12]{};
        usize digitCount = 0;
        uint32 value = mergedCount;
        do
        {
            digits[digitCount++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        for (usize i = 0; i < digitCount; ++i)
        {
            text[length++] = digits[digitCount - 1 - i];
        }
        append(" source materials merged into the nearest kept slot");
        (void)inoutLog.Add(DiagnosticSeverity::Warning, codes::MATERIALS_MERGED, {}, 0, std::string_view(text, length));
    }

    // 4. Assign placements to slots within the vertex and index budgets, largest first.
    uint32 totalVertices = 0;
    for (const uint32 index : order)
    {
        Placement& placement = plan.Placements[index];
        const IrShape& shape = scene.Shapes[placement.Shape];
        const IrMesh& mesh = scene.Meshes[shape.Mesh];
        uint32 slot = emissiveSlot;
        if (IsEmissive(shape) == false)
        {
            for (const MaterialUse& use : uses)
            {
                if (use.Material == shape.Material)
                {
                    slot = use.Slot;
                    break;
                }
            }
        }
        PlannedMaterial& planned = plan.Materials[slot];
        const usize vertices = mesh.GetVertexCount();
        const usize indices = mesh.Indices.GetSize();
        if (totalVertices + vertices > format::MAX_VERTICES || planned.IndexCount + indices > format::MAX_INDICES)
        {
            ++plan.DroppedPlacements;
            plan.DroppedVertices += static_cast<uint32>(vertices);
            (void)inoutLog.Add(DiagnosticSeverity::Warning,
                               placement.IsInstanced == true ? codes::INSTANCES_DROPPED : codes::GEOMETRY_DROPPED,
                               {},
                               0,
                               "placement left out by the vertex/index budget (smallest area first)");
            continue;
        }
        placement.IsKept = true;
        totalVertices += static_cast<uint32>(vertices);
        planned.VertexCount += static_cast<uint32>(vertices);
        planned.IndexCount += static_cast<uint32>(indices);
        if (planned.IsEmissive == false)
        {
            planned.Area += placement.Area;
        }
        if (planned.Placements.TryAdd(index) == false)
        {
            return Status::OutOfMemory;
        }
    }
    // Slots that received no geometry are removed (they would be empty meshes).
    for (usize slot = plan.Materials.GetSize(); slot > 0; --slot)
    {
        if (plan.Materials[slot - 1].Placements.IsEmpty() == true)
        {
            plan.Materials.RemoveAt(slot - 1);
        }
    }
    if (plan.Materials.IsEmpty() == true)
    {
        (void)inoutLog.Add(DiagnosticSeverity::Error, codes::LIMIT_EXCEEDED, {}, 0, "no placement fits the budget");
        return Status::Invalid;
    }
    // Placement lists follow source order so baking is stable and readable.
    for (PlannedMaterial& planned : plan.Materials)
    {
        foundation::Sort(planned.Placements.GetData(),
                         planned.Placements.GetSize(),
                         [](uint32 left, uint32 right) noexcept { return left < right; });
    }
    outPlan = static_cast<CookPlan&&>(plan);
    return Status::Ok;
}
} // namespace ludus::content::scene_cook::importers
