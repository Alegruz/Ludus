#include <ludus/foundation/base/config.h>
#include <ludus/foundation/threading/jobs.hpp>

#if !defined(LUDUS_PLATFORM_WEB) || defined(__EMSCRIPTEN_PTHREADS__)
#    error "The threading browser baseline must use the serial backend"
#endif

using namespace ludus::foundation;
using namespace ludus::foundation::threading;

namespace
{
JobOutcome Increment(void* context) noexcept
{
    ++*static_cast<uint32*>(context);
    return JobOutcome::Succeeded;
}
JobOutcome Fail(void*) noexcept
{
    return JobOutcome::Failed;
}
} // namespace

int main()
{
    uint32 count = 0;
    JobGraph graph;
    JobSystem system;
    JobHandle first{};
    JobHandle second{};
    JobOutcome outcome{};
    if (system.Initialize({1}) != JobStatus::Unsupported || system.Initialize() != JobStatus::Ok ||
        graph.Initialize({2, 1}) != JobStatus::Ok || graph.Add(Increment, &count, first) != JobStatus::Ok ||
        graph.Add(Increment, &count, second) != JobStatus::Ok || graph.DependsOn(second, first) != JobStatus::Ok ||
        graph.Seal() != JobStatus::Ok || system.Submit(graph) != JobStatus::Ok ||
        system.Wait(outcome, false) != JobStatus::Ok || outcome != JobOutcome::Succeeded || count != 2)
    {
        return 1;
    }
    const JobHandle stale = first;
    if (graph.Reset() != JobStatus::Ok || graph.GetOutcome(stale, outcome) != JobStatus::InvalidHandle ||
        graph.Add(Fail, nullptr, first) != JobStatus::Ok || graph.Add(Increment, &count, second) != JobStatus::Ok ||
        graph.DependsOn(second, first) != JobStatus::Ok || graph.Seal() != JobStatus::Ok ||
        system.Submit(graph) != JobStatus::Ok || system.Wait(outcome) != JobStatus::Ok ||
        outcome != JobOutcome::Failed || graph.GetOutcome(second, outcome) != JobStatus::Ok ||
        outcome != JobOutcome::Blocked || count != 2)
    {
        return 2;
    }
    return system.Shutdown() == JobStatus::Ok ? 0 : 3;
}
