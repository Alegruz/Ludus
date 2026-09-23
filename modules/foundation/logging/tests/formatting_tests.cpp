#include <ludus/foundation/logging/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

#if defined(_WIN32)
#    include <process.h>
#    define LUDUS_TEST_GETPID _getpid
#else
#    include <unistd.h>
#    define LUDUS_TEST_GETPID ::getpid
#endif

namespace
{

// Read an entire file into a string. Returns empty if it does not exist.
std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return {};
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// A unique temp directory per test invocation so concurrent CTest runs and
// repeated runs never collide.
std::filesystem::path make_temp_log_dir(const std::string& tag)
{
    const auto base =
        std::filesystem::temp_directory_path() / std::format("ludus_log_test_{}_{}", tag, LUDUS_TEST_GETPID());
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
    return base;
}

using namespace ludus::foundation::logging;

// Helper to find the single session .log file in a directory.
std::filesystem::path find_log_file(const std::filesystem::path& dir)
{
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".log")
        {
            return entry.path();
        }
    }
    return {};
}

inline constexpr LogCategory LogFormatTest{"FormatTest"};

} // namespace

// Test backend formatting independently of per-preset macro stripping.
TEST_CASE("format arguments are substituted into the message", "[logging][formatting]")
{
    const auto dir = make_temp_log_dir("fmt_args");

    LogConfig config{};
    config.GlobalLevel = LogLevel::Trace;
    config.EnableConsole = false;
    config.EnableDebugger = false;
    config.EnableFile = true;
    config.Directory = dir;
    LogSystem::Initialize(config);

    Log(LogLevel::Info, LogFormatTest, std::source_location::current(), "Window created: {}x{}", 1280, 720);
    LogSystem::Flush();
    LogSystem::Shutdown();

    const std::string contents = read_file(find_log_file(dir));
    CHECK(contents.find("Window created: 1280x720") != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("source metadata is appended for warnings and above", "[logging][formatting]")
{
    const auto dir = make_temp_log_dir("fmt_source");

    LogConfig config{};
    config.GlobalLevel = LogLevel::Trace;
    config.EnableConsole = false;
    config.EnableDebugger = false;
    config.EnableFile = true;
    config.Directory = dir;
    LogSystem::Initialize(config);

    Log(LogLevel::Info, LogFormatTest, std::source_location::current(), "info line");
    LUDUS_LOG_WARN(LogFormatTest, "warn line");
    LogSystem::Flush();
    LogSystem::Shutdown();

    const std::string contents = read_file(find_log_file(dir));
    // The warning line carries a "file:line" suffix; the info line does not.
    CHECK(contents.find("formatting_tests.cpp") != std::string::npos);
    // Info line present without source suffix on its own line.
    CHECK(contents.find("info line") != std::string::npos);
    CHECK(contents.find("warn line") != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("levels render with fixed-width labels", "[logging][formatting]")
{
    CHECK(ToPaddedString(LogLevel::Info) == "INFO ");
    CHECK(ToPaddedString(LogLevel::Warning) == "WARN ");
    CHECK(ToPaddedString(LogLevel::Trace) == "TRACE");
    CHECK(ToPaddedString(LogLevel::Fatal) == "FATAL");
}
