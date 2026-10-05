#include "consumer_reflection_schema.hpp"

bool ExerciseInstalledReflection() noexcept
{
    using namespace ludus::foundation;
    using namespace ludus::foundation::reflection;
    consumer::Settings source;
    uint8 storage[1024]{};
    std::span<const uint8> output;
    Diagnostic error;
    if (consumer::WriteSettingsJson(source, storage, output, error) != Status::Ok)
    {
        return false;
    }
    consumer::Settings restored;
    restored.Seed = 0;
    if (consumer::ReadSettingsJson({reinterpret_cast<const char*>(output.data()), output.size()},
                                   restored,
                                   GetSystemAllocationDomain(),
                                   error) != Status::Ok ||
        restored.Seed != source.Seed)
    {
        return false;
    }
    const Edit edit{ .FieldId = 2, .Data = { .Type = Kind::Float32, .Real = 4 } };
    return consumer::PrepareSettings(restored, {&edit, 1}, restored, error) == Status::Ok && restored.Speed == 4;
}
