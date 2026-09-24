// Perfetto/Chrome JSON export shape (final design §16) and incomplete-scope
// honesty (§6, gate G13).
#include <ludus/foundation/profiling/profiling.hpp>
#include <ludus/foundation/profiling/trace_system.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace ludus::foundation::profiling;
using ludus::foundation::uint64;

namespace
{

std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
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
