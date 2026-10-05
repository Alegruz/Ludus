#include <ludus/foundation/config/json.hpp>
#include <ludus/runtime/configuration/host_options.hpp>

bool ExerciseInstalledConfiguration() noexcept
{
    using namespace ludus::foundation;
    using namespace config;
    Context context;
    Diagnostic error;
    if (context.Initialize(ludus::runtime::configuration::HostSchema(), GetSystemAllocationDomain(), error) !=
        Status::Ok)
    {
        return false;
    }
    const Assignment edits[]{{ .Name = "host.max_frames", .Data = Value::FromUint64(42) }};
    if (context.Prepare(Layer::Launch, edits, false, context.Revision(), error) != Status::Ok ||
        context.Commit(context.Revision(), error) != Status::Ok)
    {
        return false;
    }
    ludus::runtime::configuration::HostOptions options;
    uint8 bytes[2048]{};
    std::span<const uint8> output;
    return ludus::runtime::configuration::ReadHostOptions(context, options) == Status::Ok && options.MaxFrames == 42 &&
           WriteLayer(context, Layer::Launch, ludus::runtime::configuration::HOST_SCHEMA, bytes, output) == Status::Ok;
}
