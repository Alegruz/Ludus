// Perfetto/Chrome JSON export shape (final design §16) and incomplete-scope
// honesty (§6, gate G13).
#include <ludus/foundation/profiling/integration.hpp>
#include <ludus/foundation/profiling/profiling.hpp>
#include <ludus/foundation/profiling/trace_system.hpp>

#include "internal/recorder.hpp"
#include "internal/trace_chunk.hpp"
#include "internal/trace_event.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>

// Only meaningful with profiling compiled in; a Release build produces no test
// cases here (the compile-out contract lives in disabled_tests.cpp).
#if LUDUS_PROFILING_ENABLED

using namespace ludus::foundation::profiling;
using ludus::foundation::uint64;
using ludus::foundation::usize;

namespace
{

// Read a whole file via <cstdio> (<iostream>/<sstream> are banned by AGENTS.md;
// <cstdio> is the allowed choice, matching the rest of the module).
std::string readFile(const std::string& path)
{
    std::string contents;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
    {
        return contents;
    }
    char buffer[4096];
    usize read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        contents.append(buffer, read);
    }
    std::fclose(file);
    return contents;
}

std::string tempPath(const char* name)
{
    // CTest runs in the build tree; a relative file is fine and self-cleaning
    // enough for a unit test. Keep names distinct per case.
    return std::string("ludus_profiling_") + name + ".json";
}

} // namespace

TEST_CASE("export produces well-formed Chrome trace JSON with named B/E slices", "[profiling][export]")
{
    RegisterThreadForTrace("Main");
    REQUIRE(BeginCapture(16));
    {
        LUDUS_PROFILE_SCOPE(RenderScene);
        {
            LUDUS_PROFILE_SCOPE(ShadowMaps);
        }
        LUDUS_PROFILE_FRAME();
    }
    EndCapture();

    const std::string path = tempPath("basic");
    REQUIRE(ExportPerfettoTrace(path));

    const std::string json = readFile(path);
    CHECK(json.find("\"traceEvents\"") != std::string::npos);
    CHECK(json.find("RenderScene") != std::string::npos);
    CHECK(json.find("ShadowMaps") != std::string::npos);
    CHECK(json.find("\"ph\":\"B\"") != std::string::npos);
    CHECK(json.find("\"ph\":\"E\"") != std::string::npos);
    CHECK(json.find("FrameMark") != std::string::npos);
    // Balanced braces sanity: object opens and closes.
    CHECK(json.front() == '{');
    CHECK(json.find("displayTimeUnit") != std::string::npos);

    std::remove(path.c_str());
}

TEST_CASE("equal-tick events keep recording order across a chunk boundary", "[profiling][export]")
{
    // A fast clock gives an End and the next Begin the same tick. Emit an outer
    // scope around inner pairs at one fixed tick, sized so the first chunk ends
    // on an inner Begin; its End and the outer End land in the next chunk.
    using ludus::foundation::uint16;
    using ludus::foundation::uint32;
    RegisterThreadForTrace("Main");
    REQUIRE(BeginCapture(8));
    static constexpr ZoneDescriptor OUTER_ZONE{std::string_view{"BoundaryOuter"}};
    static constexpr ZoneDescriptor INNER_ZONE{std::string_view{"BoundaryInner"}};
    ludus::foundation::profiling::detail::RegisterSite(OUTER_ZONE);
    ludus::foundation::profiling::detail::RegisterSite(INNER_ZONE);
    constexpr uint64 FIXED_TICKS = 1000;
    internal::TraceRecorder& recorder = internal::TraceRecorder::Instance();
    usize rejectedEvents = 0;
    const auto emit = [&recorder, &rejectedEvents](uint32 siteId, internal::TraceEventKind kind) {
        if (recorder.Emit({FIXED_TICKS, siteId, static_cast<uint16>(kind), 0, 0}) == false)
        {
            ++rejectedEvents;
        }
    };
    const usize innerPairs = (internal::kEventsPerChunk - 2) / 2;
    emit(OUTER_ZONE.SiteId, internal::TraceEventKind::Begin);
    for (usize pair = 0; pair < innerPairs; ++pair)
    {
        emit(INNER_ZONE.SiteId, internal::TraceEventKind::Begin);
        emit(INNER_ZONE.SiteId, internal::TraceEventKind::End);
    }
    emit(INNER_ZONE.SiteId, internal::TraceEventKind::Begin);
    emit(INNER_ZONE.SiteId, internal::TraceEventKind::End);
    emit(OUTER_ZONE.SiteId, internal::TraceEventKind::End);
    EndCapture();
    REQUIRE(rejectedEvents == 0);

    const std::string path = tempPath("chunk_boundary");
    REQUIRE(ExportPerfettoTrace(path));
    const std::string json = readFile(path);
    std::remove(path.c_str());

    // One event object per line; replay the B/E stream as a viewer would.
    usize depth = 0;
    usize begins = 0;
    bool isNestingValid = true;
    usize lineStart = 0;
    while (lineStart < json.size())
    {
        usize lineEnd = json.find('\n', lineStart);
        if (lineEnd == std::string::npos)
        {
            lineEnd = json.size();
        }
        const std::string line = json.substr(lineStart, lineEnd - lineStart);
        if (line.find("\"ph\":\"B\"") != std::string::npos)
        {
            ++depth;
            ++begins;
        }
        else if (line.find("\"ph\":\"E\"") != std::string::npos)
        {
            isNestingValid = isNestingValid == true && depth > 0;
            depth = depth > 0 ? depth - 1 : 0;
        }
        lineStart = lineEnd + 1;
    }
    CHECK(isNestingValid == true);
    CHECK(depth == 0);
    CHECK(begins == static_cast<usize>(innerPairs + 2));
    CHECK(json.find("\"incomplete\":true") == std::string::npos);
}

TEST_CASE("export of an empty capture reports failure, not a crash", "[profiling][export]")
{
    RegisterThreadForTrace("Main");
    REQUIRE(BeginCapture(4));
    EndCapture();
    // Nothing recorded on this thread -> nothing to export.
    const std::string path = tempPath("empty");
    CHECK_FALSE(ExportPerfettoTrace(path));
    std::remove(path.c_str());
}

TEST_CASE("an open scope at capture end is exported as incomplete, not fabricated", "[profiling][export][incomplete]")
{
    // Emit an unmatched Begin by leaking a heap ScopedZone we never destroy
    // before EndCapture, simulating a crash/early-termination mid-scope (§6/G13).
    RegisterThreadForTrace("Main");
    REQUIRE(BeginCapture(8));

    // A heap ScopedZone we never destroy before EndCapture leaves an unmatched
    // Begin (as a crash mid-scope would). Register the site so the Begin resolves
    // its name, exactly as the macro does on first use.
    static constexpr ZoneDescriptor kOpenDesc{std::string_view{"NeverClosed"}};
    ludus::foundation::profiling::detail::RegisterSite(kOpenDesc);
    auto* leaked = new ScopedZone(kOpenDesc);

    EndCapture();
    const std::string path = tempPath("incomplete");
    REQUIRE(ExportPerfettoTrace(path));

    const std::string json = readFile(path);
    // The synthesized close must be marked incomplete rather than presented as a
    // real measured duration (§6/G13).
    CHECK(json.find("\"incomplete\":true") != std::string::npos);
    // The open Begin resolved its site name.
    CHECK(json.find("NeverClosed") != std::string::npos);

    std::remove(path.c_str());
    // Let the leaked zone destruct without an active capture (no-op emit).
    delete leaked;
}

TEST_CASE("integration emission exports site slices, caller flows, counters and thread names",
          "[profiling][export][integration]")
{
    static constexpr ZoneDescriptor SLICE_ZONE{std::string_view{"IntegrationSlice"}};
    static constexpr ZoneDescriptor FLOW_ZONE{std::string_view{"IntegrationFlow"}};
    static constexpr ZoneDescriptor COUNTER_ZONE{std::string_view{"IntegrationCounter"}};
    constexpr uint64 FLOW_ID = CALLER_FLOW_ID_BIT | 42u;
    RegisterThreadForTrace("Main");
    CHECK(IsCaptureActive() == false);
    // Inactive: nothing is recorded.
    BeginSite(SLICE_ZONE.SiteId);
    EndSite(SLICE_ZONE.SiteId);
    REQUIRE(BeginCapture(8));
    CHECK(IsCaptureActive() == true);
    RegisterSite(SLICE_ZONE);
    RegisterSite(FLOW_ZONE);
    RegisterSite(COUNTER_ZONE);
    BeginSite(SLICE_ZONE.SiteId);
    StartFlow(FLOW_ZONE, FLOW_ID);
    SampleCounter(COUNTER_ZONE, 7);
    FinishFlow(FLOW_ZONE, FLOW_ID);
    EndSite(SLICE_ZONE.SiteId);
    EndCapture();
    CHECK(GetTraceHealth().EventsRecorded == 5);

    const std::string path = tempPath("integration");
    REQUIRE(ExportPerfettoTrace(path));
    const std::string json = readFile(path);
    std::remove(path.c_str());
    CHECK(json.find("\"ph\":\"M\",\"name\":\"thread_name\",\"args\":{\"name\":\"Main\"}") != std::string::npos);
    CHECK(json.find("\"ph\":\"B\",\"name\":\"IntegrationSlice\"") != std::string::npos);
    CHECK(json.find("\"ph\":\"E\",\"name\":\"IntegrationSlice\"") != std::string::npos);
    const std::string flowId = "\"id\":" + std::to_string(FLOW_ID);
    // Start and finish carry the same name and ID so viewers can pair them.
    CHECK(json.find("\"ph\":\"s\",\"name\":\"IntegrationFlow\",\"cat\":\"flow\"," + flowId) != std::string::npos);
    CHECK(json.find("\"ph\":\"f\",\"name\":\"IntegrationFlow\",\"cat\":\"flow\"," + flowId) != std::string::npos);
    CHECK(json.find("\"ph\":\"C\",\"name\":\"IntegrationCounter\",\"args\":{\"value\":7}") != std::string::npos);
}

#endif // LUDUS_PROFILING_ENABLED
