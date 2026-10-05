#include "consumer_reflection_schema.hpp"

// Exercise the entire generated persistence closure when linking a MODULE;
// executable-only SDK tests miss non-PIC/TLS relocations in static archives.
extern "C" __attribute__((visibility("default"))) bool ExerciseModuleReflection() noexcept
{
    using namespace ludus::foundation;
    consumer::Settings source;
    uint8 bytes[1024]{};
    std::span<const uint8> output;
    reflection::Diagnostic error;
    return consumer::WriteSettingsJson(source, bytes, output, error) == reflection::Status::Ok;
}
