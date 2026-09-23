#pragma once

#include "internal/sink.hpp"

#include <string>

namespace ludus::foundation::logging::internal
{

// Writes human-readable lines to stdout/stderr (spec section 19). Warning and
// above go to stderr; lower levels go to stdout. ANSI color is enabled only when
// the corresponding stream is a TTY, and disabled automatically on redirect.
class ConsoleSink final : public ILogSink
{
public:
    ConsoleSink();

    SinkStatus Write(const LogRecordView& record) noexcept override;
    SinkStatus Flush() noexcept override;

private:
    bool mStdoutIsTty = false;
    bool mStderrIsTty = false;

    // Reused across write() calls to avoid per-line allocation on the hot path.
    std::string mScratch;
};

} // namespace ludus::foundation::logging::internal
