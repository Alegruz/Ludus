// Direct tests of the production allocator contract supplement public-provider
// failure sweeps. Private implementation headers never enter the installed SDK.
#include <ludus/foundation/base/core.h>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>

#include <cstring>

#include "fault_allocator.h"
#include "internal/runtime.h"

namespace
{
using namespace ludus::foundation;
using namespace ludus::runtime::scripting;

bool ShrinkUnderFailure() noexcept
{
    Memory memory;
    auto* block = static_cast<uint8*>(AllocateVm(&memory, nullptr, 0, 256));
    if (block == nullptr)
    {
        return false;
    }
    std::memset(block, 42, 256);
    const usize live = memory.Live;
    const usize attempts = memory.Attempts;
    const bool aligned = reinterpret_cast<usize>(block) % 16 == 0;
    memory.FailAt = attempts + 1;
    ludus::s6::FailAllocationsFrom(1);
    const bool retained = AllocateVm(&memory, block, 256, 128) == block &&
                          AllocateVm(&memory, block, 128, 192) == block &&
                          AllocateVm(&memory, block, 192, 192) == block && ludus::s6::AllocationAttempts() == 0 &&
                          memory.Attempts == attempts && memory.Denied == 0 && memory.Live == live;
    memory.FailAt = 0;
    const bool rejected = AllocateVm(&memory, block, 192, 512) == nullptr && memory.Denied == 1 &&
                          memory.Live == live && block[0] == 42 && block[191] == 42;
    ludus::s6::StopAllocationFailure();
    const bool freed = AllocateVm(&memory, block, 192, 0) == nullptr && memory.Live == 0;
    return aligned && live > 256 && retained && rejected && freed;
}
bool GrowthAndAccounting() noexcept
{
    Memory memory;
    memory.Limit = 1024;
    auto* block = static_cast<uint8*>(AllocateVm(&memory, nullptr, 0, 64));
    if (block == nullptr)
    {
        return false;
    }
    std::memset(block, 57, 64);
    const usize old_bytes = memory.Live;
    auto* grown = static_cast<uint8*>(AllocateVm(&memory, block, 64, 128));
    if (grown == nullptr)
    {
        (void)AllocateVm(&memory, block, 64, 0);
        return false;
    }
    const bool copied =
        grown[0] == 57 && grown[63] == 57 && memory.Live > 128 && memory.Peak == old_bytes + memory.Live;
    const usize live = memory.Live;
    memory.Limit = live;
    const bool retained = AllocateVm(&memory, grown, 128, 1) == grown && memory.Live == live &&
                          AllocateVm(&memory, grown, 1, 100) == grown && memory.Live == live;
    const bool rejected =
        AllocateVm(&memory, grown, 100, 256) == nullptr && memory.Live == live && memory.Denied == 1 && grown[0] == 57;
    (void)AllocateVm(&memory, grown, 100, 0);
    return copied && retained && rejected && memory.Live == 0;
}
bool HeaderAndOverflowLimits() noexcept
{
    Memory memory;
    memory.Limit = 256;
    if (AllocateVm(&memory, nullptr, 0, 256) != nullptr || memory.Live != 0 || memory.Denied != 1)
    {
        return false;
    }
    memory.Limit = ~usize{0};
    return AllocateVm(&memory, nullptr, 0, ~usize{0}) == nullptr && memory.Live == 0 && memory.Denied == 2;
}
} // namespace

int main()
{
    logging::LogConfig config;
    config.EnableFile = false;
    config.EnableDebugger = false;
    if (logging::LogSystem::Initialize(config).Status != logging::LogStatus::Ok)
    {
        return 2;
    }
    constexpr logging::LogCategory CATEGORY{"BehaviorS6Allocator"};
    const bool passed = ShrinkUnderFailure() && GrowthAndAccounting() && HeaderAndOverflowLimits();
    if (passed)
    {
        LUDUS_LOG_INFO(CATEGORY, "S6 PASS allocator-shrink-reuse-physical-capacity-contract");
    }
    else
    {
        LUDUS_LOG_ERROR(CATEGORY, "S6 FAIL allocator-shrink-reuse-physical-capacity-contract");
    }
    logging::LogSystem::Shutdown();
    return passed ? 0 : 1;
}
