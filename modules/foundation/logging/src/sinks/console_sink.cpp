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

[[nodiscard]] bool stream_is_tty(std::FILE* stream) noexcept
{
    const int fd = LUDUS_FILENO(stream);
    return fd >= 0 && LUDUS_ISATTY(fd) != 0;
}

} // namespace

ConsoleSink::ConsoleSink() : stdout_is_tty_(stream_is_tty(stdout)), stderr_is_tty_(stream_is_tty(stderr)), scratch_()
{
    scratch_.reserve(256);
}

void ConsoleSink::write(const LogRecordView& record) noexcept
{
    // Warning and above are diagnostics that belong on stderr so they interleave
    // correctly with other error output and survive stdout redirection.
    std::FILE* stream = record.level >= LogLevel::Warning ? stderr : stdout;
    const bool use_color = stream == stderr ? stderr_is_tty_ : stdout_is_tty_;

    // format_console_line can throw only via std::string growth (bad_alloc); if
    // that happens we simply drop the line rather than propagate into engine
    // code, honoring the noexcept contract.
    try {
        format_console_line(record, use_color, scratch_);
        scratch_.push_back('\n');
        std::fwrite(scratch_.data(), 1, scratch_.size(), stream);
    }
    catch (...) {
        // Best-effort: nothing safe left to do on the console path.
    }
}

void ConsoleSink::flush() noexcept
{
    std::fflush(stdout);
    std::fflush(stderr);
}

} // namespace ludus::foundation::logging::internal
