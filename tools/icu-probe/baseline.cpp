// Size baseline for the private L0 probe: the same Ludus logging, UTF-8 and time
// closure without ICU code or data. See docs/architecture/localization-l0.md.

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>
#include <ludus/foundation/strings/utf8.hpp>
#include <ludus/foundation/time/time.hpp>

#include <string_view>

int main()
{
    using namespace ludus::foundation;
    using namespace ludus::foundation::logging;
    LogConfig config;
    config.EnableFile = false;
    config.EnableDebugger = false;
    const LogInitResult initialized = LogSystem::Initialize(config);
    if (initialized.Status != LogStatus::Ok)
    {
        return 2;
    }
    constexpr LogCategory LOG_L0{"LocalizationL0"};
    Utf8View view;
    usize errorOffset = 0;
    const bool isValid = ValidateUtf8(std::string_view("baseline"), view, errorOffset) == StringStatus::Ok;
    LUDUS_LOG_INFO(LOG_L0, "L0 BASELINE valid={} ticks={}", isValid, time::NowTicks());
    LogSystem::Shutdown();
    return isValid == true ? 0 : 1;
}
