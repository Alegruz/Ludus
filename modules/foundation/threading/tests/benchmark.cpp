// Submit-to-Wait timing of 256 independent jobs over worker counts and grain
// sizes, with every output compared with the serial oracle. `--tracing` adds the
// T2 tracing-overhead comparison; see docs/development/threading-evidence.md.
#include <ludus/foundation/containers/sort.hpp>
#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/profiling/clock.hpp>
#include <ludus/foundation/profiling/trace_system.hpp>
#include <ludus/foundation/threading/job_profiling.hpp>
#include <ludus/foundation/threading/jobs.hpp>

#include <cstdio>
#include <cstring>

using namespace ludus::foundation;
using namespace ludus::foundation::threading;

namespace
{
constexpr uint32 JOB_COUNT = 256;
constexpr uint32 SAMPLES = 11;
// 256 jobs x 5 events x 12 submissions x 15 configurations, plus partial chunks.
constexpr usize CAPTURE_CHUNKS = 512;
constexpr uint32 MODE_COUNT = 2;

struct Work
{
    uint64 Output = 0;
    uint32 Iterations = 0;
    uint32 Seed = 0;
};
JobOutcome Compute(void* context) noexcept
{
    Work& work = *static_cast<Work*>(context);
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

// One system and graph over its own job data, so two modes can be interleaved.
struct Bench
{
    StaticArray<Work, JOB_COUNT> Data{};
    StaticArray<uint64, SAMPLES> Times{};
    JobGraph Graph;
    JobSystem System;
};

[[nodiscard]] bool Initialize(Bench& outBench, uint32 workers, const JobSystemHooks& hooks, uint32 iterations)
{
    if (outBench.Graph.Initialize({JOB_COUNT, 0}) != JobStatus::Ok ||
        outBench.System.Initialize({ .WorkerCount = workers, .Hooks = hooks }) != JobStatus::Ok)
    {
        return false;
    }
    const JobLabel label = LUDUS_JOB_LABEL(BenchmarkCompute);
    for (uint32 index = 0; index < JOB_COUNT; ++index)
    {
        outBench.Data[index].Seed = index;
        outBench.Data[index].Iterations = iterations;
        JobHandle handle{};
        if (outBench.Graph.Add(Compute, &outBench.Data[index], handle, label) != JobStatus::Ok)
        {
            return false;
        }
    }
    return outBench.Graph.Seal() == JobStatus::Ok;
}

// Runs one complete submission; sample zero is the warm-up and is not recorded.
[[nodiscard]] bool RunSample(Bench& inoutBench, uint32 sample)
{
    const uint64 begin = profiling::NowTicks();
    JobOutcome outcome{};
    if (inoutBench.System.Submit(inoutBench.Graph) != JobStatus::Ok ||
        inoutBench.System.Wait(outcome) != JobStatus::Ok || outcome != JobOutcome::Succeeded)
    {
        return false;
    }
    if (sample > 0)
    {
        inoutBench.Times[sample - 1] = profiling::NowTicks() - begin;
    }
    return true;
}

[[nodiscard]] uint64 GetMedian(Bench& inoutBench)
{
    Sort(inoutBench.Times.GetData(), SAMPLES, [](uint64 left, uint64 right) noexcept { return left < right; });
    return inoutBench.Times[SAMPLES / 2];
}

// Compares every output with the oracle; the zero-worker run defines it.
[[nodiscard]] bool
CheckOutputs(const Bench& bench, StaticArray<uint64, JOB_COUNT>& inoutOracle, bool isOracle, uint64& outChecksum)
{
    outChecksum = 0;
    for (uint32 index = 0; index < JOB_COUNT; ++index)
    {
        outChecksum ^= bench.Data[index].Output;
        if (isOracle == true)
        {
            inoutOracle[index] = bench.Data[index].Output;
        }
        if (bench.Data[index].Output != inoutOracle[index])
        {
            return false;
        }
    }
    return true;
}

int RunBaseline()
{
    for (const uint32 iterations : {0u, 10000u, 100000u})
    {
        StaticArray<uint64, JOB_COUNT> oracle{};
        for (const uint32 workers : {0u, 1u, 2u, 4u, 8u})
        {
            Bench bench;
            if (Initialize(bench, workers, {}, iterations) == false)
            {
                return 1;
            }
            // One warm-up sample, then measure whole submit-to-completion runs.
            for (uint32 sample = 0; sample <= SAMPLES; ++sample)
            {
                if (RunSample(bench, sample) == false)
                {
                    return 1;
                }
            }
            uint64 checksum = 0;
            if (CheckOutputs(bench, oracle, workers == 0, checksum) == false)
            {
                return 2;
            }
            // Standalone test/benchmark diagnostics; never used by engine code.
            std::printf("jobs=%u iterations=%u workers=%u median_ns=%llu checksum=%llu\n",
                        JOB_COUNT,
                        iterations,
                        workers,
                        static_cast<unsigned long long>(GetMedian(bench)),
                        static_cast<unsigned long long>(checksum));
        }
    }
    return 0;
}

// Interleaves a hookless system with a profiler-hooked one, alternating which runs
// first, so machine drift affects both modes alike. The capture state is the
// caller's: inactive measures idle hooks, active measures recording.
int RunTracingComparison(bool isCapturing)
{
    const JobSystemHooks profilerHooks = MakeProfilerJobHooks("Benchmark worker");
    for (const uint32 iterations : {0u, 10000u, 100000u})
    {
        StaticArray<uint64, JOB_COUNT> oracle{};
        for (const uint32 workers : {0u, 1u, 2u, 4u, 8u})
        {
            StaticArray<Bench, MODE_COUNT> benches{};
            if (Initialize(benches[0], workers, {}, iterations) == false ||
                Initialize(benches[1], workers, profilerHooks, iterations) == false)
            {
                return 1;
            }
            for (uint32 sample = 0; sample <= SAMPLES; ++sample)
            {
                const uint32 first = sample % MODE_COUNT;
                if (RunSample(benches[first], sample) == false || RunSample(benches[1 - first], sample) == false)
                {
                    return 1;
                }
            }
            uint64 checksum = 0;
            uint64 tracedChecksum = 0;
            if (CheckOutputs(benches[0], oracle, workers == 0, checksum) == false ||
                CheckOutputs(benches[1], oracle, false, tracedChecksum) == false)
            {
                return 2;
            }
            const uint64 offMedian = GetMedian(benches[0]);
            const uint64 tracedMedian = GetMedian(benches[1]);
            // Standalone test/benchmark diagnostics; never used by engine code.
            std::printf("jobs=%u iterations=%u workers=%u capture=%s off_median_ns=%llu traced_median_ns=%llu "
                        "delta_ns_per_job=%.1f checksum=%llu\n",
                        JOB_COUNT,
                        iterations,
                        workers,
                        isCapturing == true ? "active" : "inactive",
                        static_cast<unsigned long long>(offMedian),
                        static_cast<unsigned long long>(tracedMedian),
                        (static_cast<float64>(tracedMedian) - static_cast<float64>(offMedian)) / JOB_COUNT,
                        static_cast<unsigned long long>(checksum));
        }
    }
    return 0;
}
} // namespace

// Usage: ludus_threading_benchmark [--tracing [trace.json]]
int main(int argumentCount, char** arguments)
{
    if (argumentCount < 2 || std::strcmp(arguments[1], "--tracing") != 0)
    {
        return RunBaseline();
    }
    profiling::RegisterThreadForTrace("Benchmark owner");
    const int idleResult = RunTracingComparison(false);
    if (idleResult != 0)
    {
        return idleResult;
    }
    if (profiling::BeginCapture(CAPTURE_CHUNKS) == false)
    {
        return 1;
    }
    const int captureResult = RunTracingComparison(true);
    profiling::EndCapture();
    const profiling::TraceHealth health = profiling::GetTraceHealth();
    std::printf("trace events_recorded=%llu events_dropped=%llu\n",
                static_cast<unsigned long long>(health.EventsRecorded),
                static_cast<unsigned long long>(health.EventsDropped));
    if (captureResult == 0 && argumentCount > 2 && profiling::ExportPerfettoTrace(arguments[2]) == false)
    {
        return 1;
    }
    return captureResult;
}
