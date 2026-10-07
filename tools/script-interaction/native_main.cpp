#include <ludus/foundation/base/core.h>
#include <ludus/foundation/logging/log_system.hpp>

#include "acceptance.h"

namespace
{
ludus::s1::Result Faulting(ludus::s1::Transaction& txn, void* user) noexcept
{
    const auto result = ludus::s1::NativeInteraction(txn, user);
    return txn.Event.Amount == 10 ? ludus::s1::Result::ScriptFault : result;
}
} // namespace
int main()
{
    using namespace ludus::foundation::logging;
    LogConfig config;
    config.EnableFile = false;
    config.EnableDebugger = false;
    if (LogSystem::Initialize(config).Status != LogStatus::Ok)
    {
        return 2;
    }
    const bool success = ludus::s1::CommonAcceptance(ludus::s1::NativeInteraction, nullptr) &&
                         ludus::s1::DomainAcceptance() && ludus::s1::CommonFaultAcceptance(Faulting, nullptr);
    LogSystem::Shutdown();
    return success ? 0 : 1;
}
