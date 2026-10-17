// Exported Perfetto content of the JobSystem profiler adapter (milestone T2).
#include <ludus/foundation/profiling/profiling.hpp>
#include <ludus/foundation/profiling/trace_system.hpp>
#include <ludus/foundation/threading/job_profiling.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <string_view>

using namespace ludus::foundation;
using namespace ludus::foundation::threading;

TEST_CASE("Job labels carry their name and the profiler site hash", "[threading][profiling]")
{
    const JobLabel label = LUDUS_JOB_LABEL(LabelledJob);
    REQUIRE(std::string_view{label.NameOrNull} == "LabelledJob");
    REQUIRE(label.SiteId == profiling::HashZoneName("LabelledJob"));
}

#if LUDUS_PROFILING_ENABLED == 0
TEST_CASE("Profiling-disabled builds return empty hooks", "[threading][profiling]")
{
    const JobSystemHooks hooks = MakeProfilerJobHooks("Disabled worker");
    REQUIRE(hooks.WorkerStartOrNull == nullptr);
    REQUIRE(hooks.WorkerStopOrNull == nullptr);
    REQUIRE(hooks.TraceOrNull == nullptr);
    REQUIRE(hooks.ShouldTraceOrNull == nullptr);
}
#else

namespace
{
constexpr uint32 RANGE_COUNT = 4;
constexpr usize READ_CHUNK_BYTES = 4096;

JobOutcome Succeed(void*) noexcept
{
    return JobOutcome::Succeeded;
}

std::string ReadFile(const char* path)
{
    std::string contents;
    std::FILE* fileOrNull = std::fopen(path, "rb");
    if (fileOrNull == nullptr)
    {
        return contents;
    }
    char buffer[READ_CHUNK_BYTES];
    usize read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), fileOrNull)) > 0)
    {
        contents.append(buffer, read);
    }
    std::fclose(fileOrNull);
    return contents;
}

// The exporter writes one event object per line.
uint32 CountLines(const std::string& json, std::string_view first, std::string_view second = {})
{
    uint32 count = 0;
    usize begin = 0;
    while (begin < json.size())
    {
        usize end = json.find('\n', begin);
        if (end == std::string::npos)
        {
            end = json.size();
        }
        const std::string_view line(json.data() + begin, end - begin);
        if (line.find(first) != std::string_view::npos && line.find(second) != std::string_view::npos)
        {
            ++count;
        }
        begin = end + 1;
    }
    return count;
}

// Returns the "id" values of every line with the given phase, in file order.
std::string CollectFlowIds(const std::string& json, std::string_view phase)
{
    std::string ids;
    usize position = 0;
    while ((position = json.find(phase, position)) != std::string::npos)
    {
        const usize lineEnd = json.find('\n', position);
        const usize idStart = json.find("\"id\":", position);
        REQUIRE(idStart < lineEnd);
        const usize valueStart = idStart + std::string_view("\"id\":").size();
        const usize valueEnd = json.find_first_of(",}", valueStart);
        ids.append(json, valueStart, valueEnd - valueStart);
        ids += ';';
        position = lineEnd;
    }
    return ids;
}
} // namespace

TEST_CASE("Profiler hooks export named workers, job slices, dispatch flows and ready depth", "[threading][profiling]")
{
    const JobSystemHooks hooks = MakeProfilerJobHooks("Trace worker");
    REQUIRE(hooks.ShouldTraceOrNull != nullptr);
    REQUIRE(hooks.ShouldTraceOrNull(hooks.ContextOrNull) == false);

    profiling::RegisterThreadForTrace("Owner");
    REQUIRE(profiling::BeginCapture(16) == true);
    REQUIRE(hooks.ShouldTraceOrNull(hooks.ContextOrNull) == true);
    {
        JobSystem system;
        REQUIRE(system.Initialize({ .WorkerCount = 2, .Hooks = hooks }) == JobStatus::Ok);
        JobGraph graph;
        REQUIRE(graph.Initialize({ .MaxJobs = RANGE_COUNT + 1, .MaxDependencies = RANGE_COUNT }) == JobStatus::Ok);
        JobHandle prepare{};
        REQUIRE(graph.Add(Succeed, nullptr, prepare, LUDUS_JOB_LABEL(TracePrepare)) == JobStatus::Ok);
        for (uint32 range = 0; range < RANGE_COUNT; ++range)
        {
            JobHandle handle{};
            REQUIRE(graph.Add(Succeed, nullptr, handle, LUDUS_JOB_LABEL(TraceRange)) == JobStatus::Ok);
            REQUIRE(graph.DependsOn(handle, prepare) == JobStatus::Ok);
        }
        REQUIRE(graph.Seal() == JobStatus::Ok);
        REQUIRE(system.Submit(graph) == JobStatus::Ok);
        JobOutcome outcome{};
        REQUIRE(system.Wait(outcome, false) == JobStatus::Ok);
        REQUIRE(outcome == JobOutcome::Succeeded);
        // Shutdown unregisters the workers, publishing their partial chunks.
        REQUIRE(system.Shutdown() == JobStatus::Ok);
    }
    profiling::EndCapture();
    REQUIRE(profiling::GetTraceHealth().EventsDropped == 0);
    const char* path = "ludus_threading_job_trace.json";
    REQUIRE(profiling::ExportPerfettoTrace(path) == true);
    const std::string json = ReadFile(path);

    CHECK(CountLines(json, "\"thread_name\"", "\"Trace worker 0\"") == 1);
    CHECK(CountLines(json, "\"thread_name\"", "\"Trace worker 1\"") == 1);
    CHECK(CountLines(json, "\"ph\":\"B\"", "\"TracePrepare\"") == 1);
    CHECK(CountLines(json, "\"ph\":\"B\"", "\"TraceRange\"") == RANGE_COUNT);
    CHECK(CountLines(json, "\"ph\":\"B\"", "\"JobSystem::Submit\"") == 1);
    CHECK(CountLines(json, "\"ph\":\"B\"", "\"JobSystem::Wait\"") == 1);
    CHECK(CountLines(json, "\"ph\":\"E\"") == CountLines(json, "\"ph\":\"B\""));
    CHECK(CountLines(json, "\"ph\":\"E\"", "incomplete") == 0);
    // One flow from Submit to the root, one per prerequisite edge.
    CHECK(CountLines(json, "\"ph\":\"s\"", "\"JobDispatch\"") == RANGE_COUNT + 1);
    CHECK(CountLines(json, "\"ph\":\"f\"", "\"JobDispatch\"") == RANGE_COUNT + 1);
    // Sampled at Submit and at each of the five job starts.
    CHECK(CountLines(json, "\"ph\":\"C\"", "\"JobReadyDepth\"") == RANGE_COUNT + 2);

    const std::string starts = ";" + CollectFlowIds(json, "\"ph\":\"s\"");
    const std::string finishes = CollectFlowIds(json, "\"ph\":\"f\"");
    usize begin = 0;
    uint32 matched = 0;
    while (begin < finishes.size())
    {
        const usize end = finishes.find(';', begin);
        const std::string id = ";" + finishes.substr(begin, end - begin + 1);
        CHECK(starts.find(id) != std::string::npos);
        ++matched;
        begin = end + 1;
    }
    CHECK(matched == RANGE_COUNT + 1);
    std::remove(path);
}

#endif
