#include <ludus/physics/fluid/field.h>

bool ExerciseInstalledFluid() noexcept
{
    using namespace ludus::physics::fluid;
    Field field;
    if (field.TryInitialize({}) != Status::Success || field.TryAddStroke({{-10, 0}, {10, 0}}) != Status::Success)
    {
        return false;
    }
    StepInfo info;
    Sample sample;
    if (field.TryAdvance(1.0 / 60.0, info) != Status::Success || field.TrySample({}, sample) != Status::Success)
    {
        return false;
    }
    const auto mass = field.GetDiagnostics().IntegratedHeight;
    return info.AdvancedSeconds == 1.0 / 60.0 && sample.VelocityX > 0.0 && field.GetDiagnostics().MaxHeight > 0.0 &&
           mass > -1e-9 && mass < 1e-9;
}
