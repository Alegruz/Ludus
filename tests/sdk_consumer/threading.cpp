#include <ludus/foundation/threading/jobs.hpp>

namespace
{
ludus::foundation::threading::JobOutcome Increment(void* context) noexcept
{
    ++*static_cast<ludus::foundation::uint32*>(context);
    return ludus::foundation::threading::JobOutcome::Succeeded;
}
} // namespace

int ExerciseInstalledThreading() noexcept
{
    namespace jobs = ludus::foundation::threading;
    ludus::foundation::uint32 output = 0;
    jobs::JobGraph graph;
    jobs::JobSystem system;
    jobs::JobHandle first{};
    jobs::JobHandle second{};
    if (graph.Initialize({2, 1}) != jobs::JobStatus::Ok || system.Initialize({2}) != jobs::JobStatus::Ok ||
        graph.Add(Increment, &output, first) != jobs::JobStatus::Ok ||
        graph.Add(Increment, &output, second) != jobs::JobStatus::Ok ||
        graph.DependsOn(second, first) != jobs::JobStatus::Ok || graph.Seal() != jobs::JobStatus::Ok ||
        system.Submit(graph) != jobs::JobStatus::Ok)
    {
        return 8;
    }
    jobs::JobOutcome result{};
    if (system.Wait(result) != jobs::JobStatus::Ok || result != jobs::JobOutcome::Succeeded || output != 2)
    {
        return 8;
    }
    return 0;
}
