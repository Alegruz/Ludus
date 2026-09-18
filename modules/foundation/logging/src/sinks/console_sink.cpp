#include "sinks/console_sink.hpp"

#include "internal/formatter.hpp"

#include <cstdio>

#if defined(_WIN32)
#    include <io.h>
#    define LUDUS_ISATTY _isatty
#    define LUDUS_FILENO _fileno
#else
#    include <unistd.h>
#    define LUDUS_ISATTY ::isatty
#    define LUDUS_FILENO ::fileno
#endif

namespace ludus::foundation::logging::internal {

namespace {

[[nodiscard]] bool streamIsTty(std::FILE* stream) noexcept
{
    const int fd = LUDUS_FILENO(stream);
    return fd >= 0 && LUDUS_ISATTY(fd) != 0;
}

} // namespace

ConsoleSink::ConsoleSink() : mStdoutIsTty(streamIsTty(stdout)), mStderrIsTty(streamIsTty(stderr)), mScratch()
{
    mScratch.reserve(256);
}

void ConsoleSink::Write(const LogRecordView& record) noexcept
{
    // Warning and above are diagnostics that belong on stderr so they interleave
    // correctly with other error output and survive stdout redirection.
    std::FILE* stream = record.Level >= LogLevel::Warning ? stderr : stdout;
    const bool use_color = stream == stderr ? mStderrIsTty : mStdoutIsTty;

    // FormatConsoleLine can throw only via std::string growth (bad_alloc); if
    // that happens we simply drop the line rather than propagate into engine
    // code, honoring the noexcept contract.
    try {
        FormatConsoleLine(record, use_color, mScratch);
        mScratch.push_back('\n');
        std::fwrite(mScratch.data(), 1, mScratch.size(), stream);
    }
    catch (...) {
        // Best-effort: nothing safe left to do on the console path.
    }
}

void ConsoleSink::Flush() noexcept
{
    std::fflush(stdout);
    std::fflush(stderr);
}

} // namespace ludus::foundation::logging::internal
