#include "sinks/debugger_sink.hpp"

#include "internal/formatter.hpp"

#if defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char*);
extern "C" __declspec(dllimport) int __stdcall IsDebuggerPresent(void);
#endif

namespace ludus::foundation::logging::internal
{

DebuggerSink::DebuggerSink()
{
#if defined(_WIN32)
    mActive = IsDebuggerPresent() != 0;
#endif
    mScratch.reserve(256);
}

void DebuggerSink::Write([[maybe_unused]] const LogRecordView& record) noexcept
{
#if defined(_WIN32)
    if (!mActive)
    {
        return;
    }
    // Debugger output never uses ANSI color. The engine builds with
    // -fno-exceptions, so formatting cannot throw a catchable exception.
    FormatConsoleLine(record, /*use_color=*/false, mScratch);
    mScratch.push_back('\n');
    OutputDebugStringA(mScratch.c_str());
#endif
    // Non-Windows: intentional no-op in Phase 1.
}

void DebuggerSink::Flush() noexcept
{
    // Debugger output is unbuffered from our side; nothing to flush.
}

} // namespace ludus::foundation::logging::internal
