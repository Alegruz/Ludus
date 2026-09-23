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

namespace ludus::foundation::logging::internal
{

namespace
{

[[nodiscard]] bool IsStreamTty(std::FILE* stream) noexcept
{
    const int fd = LUDUS_FILENO(stream);
    return fd >= 0 && LUDUS_ISATTY(fd) != 0;
}

} // namespace

ConsoleSink::ConsoleSink() : mStdoutIsTty(IsStreamTty(stdout)), mStderrIsTty(IsStreamTty(stderr))
{
    mScratch.reserve(256);
}

SinkStatus ConsoleSink::Write(const LogRecordView& record) noexcept
{
    // Warning and above are diagnostics that belong on stderr so they interleave
    // correctly with other error output and survive stdout redirection.
    std::FILE* stream = record.Level >= LogLevel::Warning ? stderr : stdout;
    const bool use_color = stream == stderr ? mStderrIsTty : mStdoutIsTty;

    // The reserved scratch buffer keeps ordinary lines allocation-free. The
    // caller serializes Write across producers, so mScratch is single-owner
    // here (fixes F1).
    FormatConsoleLine(record, use_color, mScratch);
    mScratch.push_back('\n');
    const usize written = std::fwrite(mScratch.data(), 1, mScratch.size(), stream);
    return written == mScratch.size() ? SinkStatus::Ok : SinkStatus::Failed;
}

SinkStatus ConsoleSink::Flush() noexcept
{
    const int a = std::fflush(stdout);
    const int b = std::fflush(stderr);
    return (a == 0 && b == 0) ? SinkStatus::Ok : SinkStatus::Failed;
}

} // namespace ludus::foundation::logging::internal
