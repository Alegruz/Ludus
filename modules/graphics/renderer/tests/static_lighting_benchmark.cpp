// Static-lighting bake timing: the legacy serial Step loop, then scheduled bakes on
// the serial executor and explicit worker budgets. Every scheduled run's charts are
// compared byte for byte with the Step oracle. `--tracing [trace.json]` instead
// measures profiler tracing overhead and job latencies (milestone T2). See
// docs/development/threading-evidence.md.
#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/sort.hpp>
#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/math/scalar.hpp>
#include <ludus/foundation/profiling/trace_system.hpp>
#include <ludus/foundation/threading/job_profiling.hpp>
#include <ludus/foundation/threading/jobs.hpp>
#include <ludus/foundation/time/time.hpp>
#include <ludus/graphics/renderer/static_lighting.hpp>
#include <ludus/runtime/resources/scene.h>

#include <atomic>
#include <cstdio>
#include <cstring>

using namespace ludus::foundation;
namespace rr = ludus::graphics::renderer;
namespace rhi = ludus::graphics::rhi;
namespace resources = ludus::resources;
namespace threading = ludus::foundation::threading;
namespace profiling = ludus::foundation::profiling;

namespace
{
constexpr uint32 GRID_CELLS = 32;    // 2048 receiver triangles.
constexpr uint32 EMITTER_COUNT = 13; // Single-triangle area emitters above the grid.
constexpr uint32 SAMPLES_PER_TRIANGLE = 64;
constexpr uint32 MAX_DEPTH = 4;    // Editor bake quality.
constexpr uint32 RUNS = 21;        // Measured bakes per configuration, after one warm-up.
constexpr uint32 STEP_PATHS = 256; // The editor's former QThread step size.
constexpr usize TRIANGLE_CORNERS = 3;
constexpr uint32 PERCENTILE_50 = 50;
constexpr uint32 PERCENTILE_95 = 95;
constexpr uint32 PERCENTILE_99 = 99;
constexpr uint32 PERCENT = 100;
constexpr float64 NANOSECONDS_PER_MILLISECOND = 1e6;
constexpr float64 NANOSECONDS_PER_MICROSECOND = 1e3;
constexpr uint32 EDITOR_WORKERS = 3; // The editor preview's bake budget.
constexpr uint32 MODE_COUNT = 2;
constexpr usize CAPTURE_CHUNKS = 64;
// Preparation plus the default ranges; jobs and edges are indexed below this.
constexpr uint32 MAX_TRACED_JOBS = rr::MAX_STATIC_LIGHTING_RANGES + 1;

[[nodiscard]] bool MakeTriangleMesh(resources::SceneMesh& outMesh, math::Vector3 normal)
{
    if (outMesh.Positions.TryResize(TRIANGLE_CORNERS) == false ||
        outMesh.Normals.TryResize(TRIANGLE_CORNERS) == false ||
        outMesh.Texcoords.TryResize(TRIANGLE_CORNERS) == false || outMesh.Indices.TryResize(TRIANGLE_CORNERS) == false)
    {
        return false;
    }
    outMesh.Positions[0] = {-.3F, -.3F, 0};
    outMesh.Positions[1] = {0, .3F, 0};
    outMesh.Positions[2] = {.3F, -.3F, 0};
    for (usize i = 0; i < TRIANGLE_CORNERS; ++i)
    {
        outMesh.Normals[i] = normal;
        outMesh.Indices[i] = static_cast<uint32>(i);
    }
    return true;
}
// A 4 m point-lit receiver grid under thirteen area emitters and a partial blocker.
[[nodiscard]] bool MakeScene(resources::SceneDefinition& outScene)
{
    resources::SceneDefinition& scene = outScene;
    if (scene.Textures.TryResize(1) == false || scene.Textures[0].Pixels.TryResize(4, 255) == false ||
        scene.Materials.TryResize(2) == false || scene.Meshes.TryResize(2) == false ||
        scene.Objects.TryResize(EMITTER_COUNT + 2) == false || scene.Lights.TryResize(1) == false)
    {
        return false;
    }
    scene.Textures[0].Width = 1;
    scene.Textures[0].Height = 1;
    scene.Materials[0].Texture = 0;
    scene.Materials[0].Roughness = 1;
    scene.Materials[1].Emission = {10, 20, 30};
    scene.Materials[1].AreaSourceId = 20;
    scene.Materials[1].IsEmissionTwoSided = false;
    resources::SceneMesh& grid = scene.Meshes[0];
    const usize vertexCount = usize(GRID_CELLS + 1) * (GRID_CELLS + 1);
    if (grid.Positions.TryResize(vertexCount) == false || grid.Normals.TryResize(vertexCount) == false ||
        grid.Texcoords.TryResize(vertexCount) == false ||
        grid.Indices.TryResize(usize(GRID_CELLS) * GRID_CELLS * 2 * TRIANGLE_CORNERS) == false ||
        MakeTriangleMesh(scene.Meshes[1], {0, 0, -1}) == false)
    {
        return false;
    }
    for (uint32 y = 0; y <= GRID_CELLS; ++y)
    {
        for (uint32 x = 0; x <= GRID_CELLS; ++x)
        {
            const usize vertex = usize(y) * (GRID_CELLS + 1) + x;
            const float32 u = float32(x) / float32(GRID_CELLS);
            const float32 v = float32(y) / float32(GRID_CELLS);
            grid.Positions[vertex] = {u * 4 - 2, v * 4 - 2, 0};
            grid.Normals[vertex] = {0, 0, 1};
            grid.Texcoords[vertex] = {u, v};
        }
    }
    usize index = 0;
    for (uint32 y = 0; y < GRID_CELLS; ++y)
    {
        for (uint32 x = 0; x < GRID_CELLS; ++x)
        {
            const uint32 corner = y * (GRID_CELLS + 1) + x;
            for (const uint32 vertex : {corner,
                                        corner + 1,
                                        corner + GRID_CELLS + 2,
                                        corner,
                                        corner + GRID_CELLS + 2,
                                        corner + GRID_CELLS + 1})
            {
                grid.Indices[index++] = vertex;
            }
        }
    }
    scene.Objects[0].SourceId = 1;
    for (uint32 i = 1; i <= EMITTER_COUNT; ++i)
    {
        scene.Objects[i].SourceId = i + 1;
        scene.Objects[i].Mesh = 1;
        scene.Objects[i].Material = 1;
        scene.Objects[i].Transform.Translation = {float32(i) * .1F - .7F, 0, 2};
    }
    resources::SceneObject& blocker = scene.Objects[EMITTER_COUNT + 1];
    blocker.SourceId = 100;
    blocker.Mesh = 1;
    blocker.Transform.Columns[0].X = 2;
    blocker.Transform.Columns[1].Y = 2;
    blocker.Transform.Translation = {.5F, .5F, .25F};
    scene.Lights[0].SourceId = 200;
    scene.Lights[0].Kind = resources::SceneLightKind::Point;
    scene.Lights[0].IntensityRgb = {50, 0, 0};
    scene.Lights[0].PositionWorld = {0, 0, .5F};
    scene.Lights[0].Range = 10;
    return true;
}
[[nodiscard]] rr::StaticLightingBakeDescription MakeDescription()
{
    rr::StaticLightingBakeDescription description;
    description.SceneRevision = 1;
    description.DependencyRevision = 1;
    description.SamplesPerTriangle = SAMPLES_PER_TRIANGLE;
    description.MaxDepth = MAX_DEPTH;
    return description;
}
[[nodiscard]] bool
AreChartsEqual(const rr::StaticLightingBake& oracle, const rr::StaticLightingBake& candidate, usize objectCount)
{
    for (usize i = 0; i < objectCount; ++i)
    {
        rr::StaticLightingChart expected;
        rr::StaticLightingChart actual;
        if (oracle.GetChart(i, expected) != rhi::RasterStatus::Ready ||
            candidate.GetChart(i, actual) != rhi::RasterStatus::Ready ||
            actual.Pixels.GetSize() != expected.Pixels.GetSize() || actual.Scale != expected.Scale ||
            std::memcmp(actual.Pixels.GetDataOrNull(), expected.Pixels.GetDataOrNull(), actual.Pixels.GetSize()) != 0)
        {
            return false;
        }
    }
    return true;
}
// Nearest-rank percentile of an ascending sample set.
[[nodiscard]] float64 GetPercentile(const Array<uint64>& sorted, uint32 percentile)
{
    const usize rank = (sorted.GetSize() * percentile + PERCENT - 1) / PERCENT;
    return float64(sorted[rank == 0 ? 0 : rank - 1]) / NANOSECONDS_PER_MILLISECOND;
}
void SortAscending(Array<uint64>& inoutValues)
{
    Sort(inoutValues.GetData(), inoutValues.GetSize(), [](uint64 left, uint64 right) noexcept { return left < right; });
}
struct Samples final
{
    Array<uint64> Wall;
    Array<uint64> CriticalPath;
    Array<uint64> Work;
    Array<uint64> Dispatch;
    Array<uint64> Merge;
};
[[nodiscard]] bool Reserve(Samples& outSamples)
{
    return outSamples.Wall.TryEnsureCapacity(RUNS) == true && outSamples.CriticalPath.TryEnsureCapacity(RUNS) == true &&
           outSamples.Work.TryEnsureCapacity(RUNS) == true && outSamples.Dispatch.TryEnsureCapacity(RUNS) == true &&
           outSamples.Merge.TryEnsureCapacity(RUNS) == true;
}

// Bakes once through Submit/Finish and checks every chart against the oracle.
// Returns 0, or the benchmark's exit code for a failure.
[[nodiscard]] int RunBake(threading::JobSystem& inoutSystem,
                          const resources::SceneDefinition& scene,
                          const rr::StaticLightingBakeDescription& description,
                          const rr::StaticLightingBake& oracle,
                          uint64& outNanoseconds)
{
    rr::StaticLightingBake bake;
    const uint64 start = time::NowTicks();
    if (bake.Submit(inoutSystem, scene, description) != rhi::RasterStatus::Pending ||
        bake.Finish() != rhi::RasterStatus::Ready)
    {
        return 2;
    }
    outNanoseconds = time::NowTicks() - start;
    return AreChartsEqual(oracle, bake, scene.Objects.GetSize()) == true ? 0 : 3;
}

// Per-job timings of one traced bake, recorded from the trace hooks and forwarded
// to the profiler. Each slot is written by one thread; the scheduler's dependency
// publication orders a dispatch record before the dependent's start, and Wait
// orders every record before the owner reads it.
struct LatencyRecorder final
{
    threading::JobSystemHooks Profiler{};
    StaticArray<uint64, MAX_TRACED_JOBS> RootDispatchTicks{};
    StaticArray<uint64, MAX_TRACED_JOBS> EdgeDispatchTicks{};
    StaticArray<uint64, MAX_TRACED_JOBS> StartTicks{};
    StaticArray<uint64, MAX_TRACED_JOBS> DispatchToStart{};
    StaticArray<uint64, MAX_TRACED_JOBS> Execution{};
    std::atomic<uint32> MaxReadyDepth{0};
    uint64 WaitEnteredTicks = 0;
    uint64 WaitNanoseconds = 0;
    uint32 JobCount = 0;
};
void RecordLatency(void* context, const threading::JobTraceEvent& event) noexcept
{
    LatencyRecorder& recorder = *static_cast<LatencyRecorder*>(context);
    const uint64 now = time::NowTicks();
    switch (event.Kind)
    {
        case threading::JobTraceKind::Submitted:
            recorder.JobCount = event.JobCount;
            for (uint32 root = 0; root < event.ReadyDepth; ++root)
            {
                recorder.RootDispatchTicks[root] = now;
            }
            break;
        case threading::JobTraceKind::Started: {
            const uint64 dispatched = event.EdgeIndex == threading::NO_JOB_EDGE
                                          ? recorder.RootDispatchTicks[event.Sequence]
                                          : recorder.EdgeDispatchTicks[event.EdgeIndex];
            recorder.DispatchToStart[event.JobIndex] = now - dispatched;
            recorder.StartTicks[event.JobIndex] = now;
            uint32 depth = recorder.MaxReadyDepth.load(std::memory_order_relaxed);
            while (depth < event.ReadyDepth &&
                   recorder.MaxReadyDepth.compare_exchange_weak(depth, event.ReadyDepth, std::memory_order_relaxed) ==
                       false)
            {
            }
            break;
        }
        case threading::JobTraceKind::Signalled:
            recorder.EdgeDispatchTicks[event.EdgeIndex] = now;
            break;
        case threading::JobTraceKind::Finished:
            recorder.Execution[event.JobIndex] = now - recorder.StartTicks[event.JobIndex];
            break;
        case threading::JobTraceKind::WaitEntered:
            recorder.WaitEnteredTicks = now;
            break;
        case threading::JobTraceKind::WaitReturned:
            recorder.WaitNanoseconds = now - recorder.WaitEnteredTicks;
            break;
    }
    recorder.Profiler.TraceOrNull(recorder.Profiler.ContextOrNull, event);
}
bool ShouldRecordLatency(void* context) noexcept
{
    const LatencyRecorder& recorder = *static_cast<const LatencyRecorder*>(context);
    return recorder.Profiler.ShouldTraceOrNull(recorder.Profiler.ContextOrNull);
}
void StartLatencyWorker(void* context, uint32 workerIndex) noexcept
{
    const LatencyRecorder& recorder = *static_cast<const LatencyRecorder*>(context);
    recorder.Profiler.WorkerStartOrNull(recorder.Profiler.ContextOrNull, workerIndex);
}
void StopLatencyWorker(void* context, uint32 workerIndex) noexcept
{
    const LatencyRecorder& recorder = *static_cast<const LatencyRecorder*>(context);
    recorder.Profiler.WorkerStopOrNull(recorder.Profiler.ContextOrNull, workerIndex);
}

void PrintDistribution(const char* name, Array<uint64>& inoutValues, float64 unitNanoseconds, const char* unit)
{
    SortAscending(inoutValues);
    // Standalone benchmark diagnostics; never used by engine code.
    std::printf("%s_%s p50=%.2f p95=%.2f p99=%.2f max=%.2f samples=%llu\n",
                name,
                unit,
                GetPercentile(inoutValues, PERCENTILE_50) * NANOSECONDS_PER_MILLISECOND / unitNanoseconds,
                GetPercentile(inoutValues, PERCENTILE_95) * NANOSECONDS_PER_MILLISECOND / unitNanoseconds,
                GetPercentile(inoutValues, PERCENTILE_99) * NANOSECONDS_PER_MILLISECOND / unitNanoseconds,
                float64(inoutValues[inoutValues.GetSize() - 1]) / unitNanoseconds,
                static_cast<unsigned long long>(inoutValues.GetSize()));
}

// Interleaves bakes on a hookless system and a profiler-hooked system with the
// same worker budget, alternating which runs first in each round.
[[nodiscard]] int CompareTracing(const resources::SceneDefinition& scene,
                                 const rr::StaticLightingBakeDescription& description,
                                 const rr::StaticLightingBake& oracle,
                                 bool isCapturing)
{
    for (const uint32 workers : {0U, EDITOR_WORKERS})
    {
        StaticArray<threading::JobSystem, MODE_COUNT> systems{};
        StaticArray<Array<uint64>, MODE_COUNT> walls{};
        if (systems[0].Initialize({ .WorkerCount = workers }) != threading::JobStatus::Ok ||
            systems[1].Initialize(
            {
                .WorkerCount = workers,
                .Hooks = threading::MakeProfilerJobHooks("Bake worker"),
            }) != threading::JobStatus::Ok ||
            walls[0].TryEnsureCapacity(RUNS) == false || walls[1].TryEnsureCapacity(RUNS) == false)
        {
            return 1;
        }
        for (uint32 run = 0; run <= RUNS; ++run)
        {
            for (uint32 order = 0; order < MODE_COUNT; ++order)
            {
                const uint32 mode = (run + order) % MODE_COUNT;
                uint64 nanoseconds = 0;
                const int result = RunBake(systems[mode], scene, description, oracle, nanoseconds);
                if (result != 0)
                {
                    return result;
                }
                if (run > 0)
                {
                    static_cast<void>(walls[mode].TryAdd(nanoseconds));
                }
            }
        }
        SortAscending(walls[0]);
        SortAscending(walls[1]);
        // Standalone benchmark diagnostics; never used by engine code.
        std::printf("capture=%s workers=%u off_ms p50=%.2f p95=%.2f traced_ms p50=%.2f p95=%.2f delta_ms p50=%.2f "
                    "identical=%u\n",
                    isCapturing == true ? "active" : "inactive",
                    workers,
                    GetPercentile(walls[0], PERCENTILE_50),
                    GetPercentile(walls[0], PERCENTILE_95),
                    GetPercentile(walls[1], PERCENTILE_50),
                    GetPercentile(walls[1], PERCENTILE_95),
                    GetPercentile(walls[1], PERCENTILE_50) - GetPercentile(walls[0], PERCENTILE_50),
                    (RUNS + 1) * MODE_COUNT);
    }
    return 0;
}

// With a capture active, bakes on the editor budget through the latency recorder.
[[nodiscard]] int MeasureLatencies(const resources::SceneDefinition& scene,
                                   const rr::StaticLightingBakeDescription& description,
                                   const rr::StaticLightingBake& oracle)
{
    LatencyRecorder recorder;
    recorder.Profiler = threading::MakeProfilerJobHooks("Bake worker");
    if (recorder.Profiler.TraceOrNull == nullptr)
    {
        return 0; // Profiling compiled out: nothing to record.
    }
    threading::JobSystem system;
    const threading::JobSystemHooks hooks
    {
        .WorkerStartOrNull = StartLatencyWorker,
        .WorkerStopOrNull = StopLatencyWorker,
        .TraceOrNull = RecordLatency,
        .ShouldTraceOrNull = ShouldRecordLatency,
        .ContextOrNull = &recorder,
    };
    Array<uint64> dispatchToStart;
    Array<uint64> rangeExecution;
    Array<uint64> ownerWait;
    if (system.Initialize({ .WorkerCount = EDITOR_WORKERS, .Hooks = hooks }) != threading::JobStatus::Ok ||
        dispatchToStart.TryEnsureCapacity(usize(RUNS) * MAX_TRACED_JOBS) == false ||
        rangeExecution.TryEnsureCapacity(usize(RUNS) * MAX_TRACED_JOBS) == false ||
        ownerWait.TryEnsureCapacity(RUNS) == false)
    {
        return 1;
    }
    for (uint32 run = 0; run <= RUNS; ++run)
    {
        uint64 nanoseconds = 0;
        const int result = RunBake(system, scene, description, oracle, nanoseconds);
        if (result != 0)
        {
            return result;
        }
        if (run == 0)
        {
            continue;
        }
        for (uint32 job = 0; job < recorder.JobCount; ++job)
        {
            static_cast<void>(dispatchToStart.TryAdd(recorder.DispatchToStart[job]));
            // Job zero is preparation; the rest are triangle ranges.
            if (job > 0)
            {
                static_cast<void>(rangeExecution.TryAdd(recorder.Execution[job]));
            }
        }
        static_cast<void>(ownerWait.TryAdd(recorder.WaitNanoseconds));
    }
    // Standalone benchmark diagnostics; never used by engine code.
    std::printf("latency workers=%u jobs_per_bake=%u max_ready_depth=%u\n",
                EDITOR_WORKERS,
                recorder.JobCount,
                recorder.MaxReadyDepth.load());
    PrintDistribution("dispatch_to_start", dispatchToStart, NANOSECONDS_PER_MICROSECOND, "us");
    PrintDistribution("range_execution", rangeExecution, NANOSECONDS_PER_MILLISECOND, "ms");
    PrintDistribution("owner_wait", ownerWait, NANOSECONDS_PER_MILLISECOND, "ms");
    return 0;
}

[[nodiscard]] int RunTracingBenchmark(const resources::SceneDefinition& scene,
                                      const rr::StaticLightingBakeDescription& description,
                                      const rr::StaticLightingBake& oracle,
                                      const char* tracePathOrNull)
{
    profiling::RegisterThreadForTrace("Bake owner");
    int result = CompareTracing(scene, description, oracle, false);
    if (result != 0)
    {
        return result;
    }
    if (profiling::BeginCapture(CAPTURE_CHUNKS) == false)
    {
        return 1;
    }
    result = CompareTracing(scene, description, oracle, true);
    if (result == 0)
    {
        result = MeasureLatencies(scene, description, oracle);
    }
    // Every system above has shut down, so worker chunks are published.
    profiling::EndCapture();
    const profiling::TraceHealth health = profiling::GetTraceHealth();
    std::printf("trace events_recorded=%llu events_dropped=%llu\n",
                static_cast<unsigned long long>(health.EventsRecorded),
                static_cast<unsigned long long>(health.EventsDropped));
    if (result == 0 && tracePathOrNull != nullptr && profiling::ExportPerfettoTrace(tracePathOrNull) == false)
    {
        return 1;
    }
    return result;
}
} // namespace

// Usage: ludus_static_lighting_benchmark [--tracing [trace.json]]
int main(int argumentCount, char** arguments)
{
    resources::SceneDefinition scene;
    if (MakeScene(scene) == false)
    {
        return 1;
    }
    const bool shouldMeasureTracing = argumentCount > 1 && std::strcmp(arguments[1], "--tracing") == 0;
    const rr::StaticLightingBakeDescription description = MakeDescription();
    const usize objectCount = scene.Objects.GetSize();
    rr::StaticLightingBake oracle;
    Array<uint64> stepWall;
    if (stepWall.TryEnsureCapacity(RUNS) == false)
    {
        return 1;
    }
    // The former editor path: Initialize and Step(256) on one dedicated thread.
    for (uint32 run = 0; run <= RUNS; ++run)
    {
        rr::StaticLightingBake bake;
        const uint64 start = time::NowTicks();
        rhi::RasterStatus status = bake.Initialize(scene, description);
        while (status == rhi::RasterStatus::Pending)
        {
            status = bake.Step(STEP_PATHS);
        }
        const uint64 end = time::NowTicks();
        if (status != rhi::RasterStatus::Ready)
        {
            return 2;
        }
        if (run == 0)
        {
            rhi::RasterStatus oracleStatus = oracle.Initialize(scene, description);
            while (oracleStatus == rhi::RasterStatus::Pending)
            {
                oracleStatus = oracle.Step(STEP_PATHS);
            }
            continue;
        }
        static_cast<void>(stepWall.TryAdd(end - start));
    }
    if (shouldMeasureTracing == true)
    {
        return RunTracingBenchmark(scene, description, oracle, argumentCount > 2 ? arguments[2] : nullptr);
    }
    SortAscending(stepWall);
    rr::StaticLightingBakeProgress progress;
    oracle.GetProgress(progress);
    // Standalone benchmark diagnostics; never used by engine code.
    std::printf("scene triangles=%llu samples_per_triangle=%u depth=%u runs=%u ranges=%u\n",
                static_cast<unsigned long long>(progress.TotalSamples / SAMPLES_PER_TRIANGLE),
                SAMPLES_PER_TRIANGLE,
                MAX_DEPTH,
                RUNS,
                rr::StaticLightingScheduleDescription{}.RangeCount);
    std::printf("step  wall_ms p50=%.2f p95=%.2f p99=%.2f\n",
                GetPercentile(stepWall, PERCENTILE_50),
                GetPercentile(stepWall, PERCENTILE_95),
                GetPercentile(stepWall, PERCENTILE_99));
    float64 serialWallMedian = 0;
    for (const uint32 workers : {0U, 1U, 2U, 4U, 8U})
    {
        threading::JobSystem system;
        Samples samples;
        if (system.Initialize({ .WorkerCount = workers }) != threading::JobStatus::Ok || Reserve(samples) == false)
        {
            return 1;
        }
        for (uint32 run = 0; run <= RUNS; ++run)
        {
            rr::StaticLightingBake bake;
            const uint64 start = time::NowTicks();
            if (bake.Submit(system, scene, description) != rhi::RasterStatus::Pending ||
                bake.Finish() != rhi::RasterStatus::Ready)
            {
                return 2;
            }
            const uint64 end = time::NowTicks();
            if (AreChartsEqual(oracle, bake, objectCount) == false)
            {
                return 3;
            }
            rr::StaticLightingBakeProfile profile;
            if (bake.GetProfile(profile) != rhi::RasterStatus::Ready)
            {
                return 2;
            }
            if (run == 0)
            {
                continue;
            }
            static_cast<void>(samples.Wall.TryAdd(end - start));
            static_cast<void>(samples.CriticalPath.TryAdd(profile.PrepareNanoseconds + profile.DispatchNanoseconds +
                                                          profile.LongestRangeNanoseconds + profile.MergeNanoseconds));
            static_cast<void>(
                samples.Work.TryAdd(profile.PrepareNanoseconds + profile.RangeNanoseconds + profile.MergeNanoseconds));
            static_cast<void>(samples.Dispatch.TryAdd(profile.DispatchNanoseconds));
            static_cast<void>(samples.Merge.TryAdd(profile.MergeNanoseconds));
        }
        SortAscending(samples.Wall);
        SortAscending(samples.CriticalPath);
        SortAscending(samples.Work);
        SortAscending(samples.Dispatch);
        SortAscending(samples.Merge);
        const float64 wallMedian = GetPercentile(samples.Wall, PERCENTILE_50);
        const float64 workMedian = GetPercentile(samples.Work, PERCENTILE_50);
        const float64 criticalMedian = GetPercentile(samples.CriticalPath, PERCENTILE_50);
        if (workers == 0)
        {
            serialWallMedian = wallMedian;
        }
        // Lower bound for this budget: the span, or total work over workers plus the helping caller.
        const float64 bound = math::Max(criticalMedian, workMedian / float64(workers + 1));
        std::printf("workers=%u wall_ms p50=%.2f p95=%.2f p99=%.2f critical_ms p50=%.2f work_ms p50=%.2f "
                    "dispatch_us p50=%.1f merge_ms p50=%.2f overhead_ms p50=%.2f speedup=%.2f identical=%u\n",
                    workers,
                    wallMedian,
                    GetPercentile(samples.Wall, PERCENTILE_95),
                    GetPercentile(samples.Wall, PERCENTILE_99),
                    criticalMedian,
                    workMedian,
                    GetPercentile(samples.Dispatch, PERCENTILE_50) * 1000,
                    GetPercentile(samples.Merge, PERCENTILE_50),
                    wallMedian - bound,
                    serialWallMedian / wallMedian,
                    RUNS + 1);
    }
    return 0;
}
