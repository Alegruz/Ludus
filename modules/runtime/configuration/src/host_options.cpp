#include <ludus/runtime/configuration/host_options.hpp>

// Thanks to Lasse Staff Jensen, "A Generic Tweaker", Game Programming Gems 2,
// sec.1.18, pp.118-126: share metadata across authoring and native options.
// This adapter copies plain options and exposes no writable engine pointers.
namespace ludus::runtime::configuration
{
using namespace foundation::config;
std::span<const Descriptor> HostSchema() noexcept
{
    static const Descriptor kSchema[]{
        {
            .Name = "host.headless",
            .Default = Value::FromBool(false),
            .Help = "Run without a presentation window. Takes effect on the next launch.",
            .Units = {},
            .Choices = {},
            .Application = Apply::NextLaunch,
            .Persistent = true,
        },
        {
            .Name = "host.max_frames",
            .Default = Value::FromUint64(0),
            .Help = "Stop after this many frames; zero runs until stopped.",
            .Units = "frames",
            .Choices = {},
            .Application = Apply::NextLaunch,
            .Persistent = true,
        },
    };
    return kSchema;
}
Status ReadHostOptions(const Context& context, HostOptions& output) noexcept
{
    Binding headless, frames;
    Explanation headlessValue, framesValue;
    if (context.Bind("host.headless", headless) != Status::Ok ||
        context.Bind("host.max_frames", frames) != Status::Ok ||
        context.Explain(headless, headlessValue) != Status::Ok || context.Explain(frames, framesValue) != Status::Ok ||
        headlessValue.Active.Kind != Type::Bool || framesValue.Active.Kind != Type::Uint64)
    {
        return Status::InvalidSchema;
    }
    output = {headlessValue.Active.Boolean, framesValue.Active.Unsigned};
    return Status::Ok;
}
} // namespace ludus::runtime::configuration
