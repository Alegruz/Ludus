#include <ludus/foundation/threading/jobs.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>

using namespace ludus::foundation;
using namespace ludus::foundation::threading;

namespace
{
constexpr uint32 JOB_COUNT = 256;
constexpr uint32 SAMPLES = 11;
struct Work
{
    uint64 Output = 0;
    uint32 Iterations = 0;
    uint32 Seed = 0;
};
JobOutcome Compute(void* context) noexcept
{
    auto& work = *static_cast<Work*>(context);
    uint64 value = work.Seed + 1;
    for (uint32 index = 0; index < work.Iterations; ++index)
    {
        value ^= value << 13;
        value ^= value >> 7;
        value ^= value << 17;
    }
    work.Output = value;
    return JobOutcome::Succeeded;
}
} // namespace

int main()
{
    for (const uint32 iterations : {0u, 10000u, 100000u})
    {
        uint64 oracle[JOB_COUNT]{};
        for (const uint32 workers : {0u, 1u, 2u, 4u, 8u})
        {
            Work data[JOB_COUNT]{};
            JobGraph graph;
            JobSystem system;
            if (graph.Initialize({JOB_COUNT, 0}) != JobStatus::Ok || system.Initialize({workers}) != JobStatus::Ok)
            {
                return 1;
            }
            for (uint32 index = 0; index < JOB_COUNT; ++index)
            {
                data[index].Seed = index;
                data[index].Iterations = iterations;
                JobHandle handle{};
                if (graph.Add(Compute, &data[index], handle) != JobStatus::Ok)
                {
                    return 1;
                }
            }
            if (graph.Seal() != JobStatus::Ok)
            {
                return 1;
            }
            int64 times[SAMPLES]{};
            // One warm-up sample, then measure whole submit-to-completion runs.
            for (uint32 sample = 0; sample <= SAMPLES; ++sample)
            {
                const auto begin = std::chrono::steady_clock::now();
                JobOutcome outcome{};
                if (system.Submit(graph) != JobStatus::Ok || system.Wait(outcome) != JobStatus::Ok ||
                    outcome != JobOutcome::Succeeded)
                {
                    return 1;
                }
                if (sample > 0)
                {
                    times[sample - 1] =
                        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin)
                            .count();
                }
            }
            std::sort(times, times + SAMPLES);
            uint64 checksum = 0;
            for (uint32 index = 0; index < JOB_COUNT; ++index)
            {
                checksum ^= data[index].Output;
                if (workers == 0)
                {
                    oracle[index] = data[index].Output;
                }
                if (data[index].Output != oracle[index])
                {
                    return 2;
                }
            }
            // Standalone test/benchmark diagnostics; never used by engine code.
            std::printf("jobs=%u iterations=%u workers=%u median_ns=%lld checksum=%llu\n",
                        JOB_COUNT,
                        iterations,
                        workers,
                        static_cast<long long>(times[SAMPLES / 2]),
                        static_cast<unsigned long long>(checksum));
        }
    }
}
