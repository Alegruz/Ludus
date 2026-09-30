#include <ludus/foundation/base/diagnostic_output.hpp>

#include <emscripten.h>

namespace ludus::foundation::diagnostics
{
namespace
{
constinit ControlState gControlState = ControlState::Unconfigured;

// clang-format off
EM_JS(int32, writeConsoleBytes, (const char* data, usize size), {
    try {
        console.error(UTF8ToString(data, size));
        return 1;
    } catch (_) {
        // JavaScript bridge errors never escape into the exception-free runtime.
        return 0;
    }
});
// clang-format on
} // namespace

bool ConfigureEmergencySocket(int) noexcept
{
    return false;
}

DeliveryStatus TryWriteEmergencyBytes(const char* data, usize size) noexcept
{
    if (size > CONTROL_MAX_PAYLOAD || data == nullptr)
    {
        return DeliveryStatus::Failed;
    }
    if (size == 0)
    {
        return DeliveryStatus::Delivered;
    }
    return writeConsoleBytes(data, size) != 0 ? DeliveryStatus::Delivered : DeliveryStatus::Failed;
}

bool WriteEmergencyBytes(const char* data, usize size) noexcept
{
    if (size == 0)
    {
        return true;
    }
    return TryWriteEmergencyBytes(data, size) == DeliveryStatus::Delivered;
}

ControlState ConfigureControlEndpoint(int) noexcept
{
    gControlState = ControlState::Failed;
    return gControlState;
}

ControlState ControlEndpointState() noexcept
{
    return gControlState;
}

ControlDecision RequestAssertDecision(uint32, const char*, usize) noexcept
{
    return ControlDecision::Terminate;
}

bool IsContinuousIntegration() noexcept
{
    // Browser assertions are always report-only and terminal. Suppress all
    // interactive debugger/helper paths, regardless of the hosting environment.
    return true;
}
} // namespace ludus::foundation::diagnostics
