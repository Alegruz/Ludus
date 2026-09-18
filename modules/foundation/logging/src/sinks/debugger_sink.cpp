#include "sinks/debugger_sink.hpp"

#include "internal/formatter.hpp"

#if defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char*);
extern "C" __declspec(dllimport) int __stdcall IsDebuggerPresent(void);
#endif

namespace ludus::foundation::logging::internal {

DebuggerSink::DebuggerSink() : active_(false), scratch_()
{
#if defined(_WIN32)
    active_ = IsDebuggerPresent() != 0;
#endif
    scratch_.reserve(256);
}

void DebuggerSink::write([[maybe_unused]] const LogRecordView& record) noexcept
{
#if defined(_WIN32)
    if (!active_) {
        return;
    }
    try {
        // Debugger output never uses ANSI color.
        format_console_line(record, /*use_color=*/false, scratch_);
        scratch_.push_back('\n');
        OutputDebugStringA(scratch_.c_str());
    }
    catch (...) {
        // Drop on allocation failure rather than propagate.
    }
#endif
    // Non-Windows: intentional no-op in Phase 1.
}

void DebuggerSink::flush() noexcept
{
    // Debugger output is unbuffered from our side; nothing to flush.
}

} // namespace ludus::foundation::logging::internal
