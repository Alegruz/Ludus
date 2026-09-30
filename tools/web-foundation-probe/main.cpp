#include <ludus/foundation/base/core.h>

#include <ludus/foundation/base/assert_format.hpp>
#include <ludus/foundation/base/diagnostic_output.hpp>
#include <ludus/foundation/base/version.hpp>

#include <string_view>

#include <emscripten.h>

using namespace ludus::foundation;

static_assert(LUDUS_PLATFORM_WEB == 1);
static_assert(LUDUS_ARCH_WASM32 == 1);
static_assert(LUDUS_ASSERT_DIALOGS_AVAILABLE == 0);
#if defined(LUDUS_PLATFORM_LINUX) || defined(LUDUS_PLATFORM_DESKTOP) || defined(__cpp_exceptions)
#    error "Browser Base must not inherit native platform or exception policy"
#endif

namespace
{
// clang-format off
EM_JS(void, failConsole, (), {
    Module.savedConsoleError = console.error;
    console.error = function () { throw new Error('controlled console failure'); };
});
EM_JS(void, restoreConsole, (), {
    console.error = Module.savedConsoleError;
});
// clang-format on

bool transportContract() noexcept
{
    using namespace diagnostics;
    char frame[CONTROL_HEADER_SIZE]{};
    ControlHeader header{};
    return !ConfigureEmergencySocket(-1) && ControlEndpointState() == ControlState::Unconfigured &&
           ConfigureControlEndpoint(-1) == ControlState::Failed &&
           RequestAssertDecision(1, nullptr, 0) == ControlDecision::Terminate &&
           TryWriteEmergencyBytes(nullptr, 1) == DeliveryStatus::Failed &&
           TryWriteEmergencyBytes("", CONTROL_MAX_PAYLOAD + 1) == DeliveryStatus::Failed &&
           EncodeControlHeader(frame, sizeof(frame), ControlMessageType::Hello, 0, 0) == CONTROL_HEADER_SIZE &&
           DecodeControlHeader(frame, sizeof(frame), header) && header.Length == 0;
}

void reportPassed() noexcept
{
    constexpr char passed[] = "[W1:after] probe returned normally";
    (void)diagnostics::WriteEmergencyBytes(passed, sizeof(passed) - 1);
}
} // namespace

int main(int argc, char** argv)
{
    const std::string_view mode = argc > 1 ? argv[1] : "normal";
    if (version_string().empty() || !transportContract())
    {
        return 2;
    }
    if (mode == "check")
    {
        int32 evaluations = 0;
        const bool checked = LUDUS_CHECK(++evaluations == 0, "browser check");
        if (checked || evaluations != 1)
        {
            return 3;
        }
        // A second report verifies the first CHECK released its ownership.
        if (LUDUS_CHECK_F(false, "value={} float={}", uint32{42}, float64{1.25}))
        {
            return 4;
        }
    }
    else if (mode == "console-failure")
    {
        failConsole();
        const auto delivered = diagnostics::TryWriteEmergencyBytes("probe", 5);
        restoreConsole();
        if (delivered != diagnostics::DeliveryStatus::Failed)
        {
            return 7;
        }
    }
    else if (mode == "assert")
    {
        int32 evaluations = 0;
        LUDUS_ASSERT(++evaluations == 0, "browser assert");
        if (evaluations != 0) // Only disabled ASSERT may reach this line.
        {
            return 5;
        }
    }
    else if (mode == "require")
    {
        LUDUS_REQUIRE(false, "browser require");
    }
    else if (mode == "fatal")
    {
        LUDUS_FATAL("browser fatal");
    }
    else if (mode == "trap")
    {
        LUDUS_DEBUG_BREAK();
    }
    else if (mode != "normal")
    {
        return 6;
    }
    reportPassed();
    return 0;
}
