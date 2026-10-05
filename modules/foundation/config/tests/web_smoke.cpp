#include <ludus/foundation/config/json.hpp>

int main()
{
    using namespace ludus::foundation;
    using namespace config;
    const Descriptor schema[]{
    {
        .Name = "probe.enabled",
        .Default = Value::FromBool(false),
        .Help = {},
        .Units = {},
        .Choices = {},
        .Persistent = true,
    }};
    Context context;
    Diagnostic error;
    const auto& domain = GetSystemAllocationDomain();
    if (context.Initialize(schema, domain, error) != Status::Ok)
    {
        return 1;
    }
    const Assignment edits[]{{ .Name = "probe.enabled", .Data = Value::FromBool(true) }};
    if (context.Prepare(Layer::Preference, edits, false, context.Revision(), error) != Status::Ok ||
        context.Commit(context.Revision(), error) != Status::Ok)
    {
        return 2;
    }
    uint8 buffer[1024]{};
    std::span<const uint8> output;
    return WriteLayer(context, Layer::Preference, "probe.v1", buffer, output) == Status::Ok ? 0 : 3;
}
