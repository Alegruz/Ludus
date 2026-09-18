#pragma once

#include "internal/sink.hpp"

#include <string>

namespace ludus::foundation::logging::internal {

// Routes records to the attached debugger's output window (spec section 18).
// On Windows this uses OutputDebugStringA when a debugger is present. On other
// platforms there is no universal equivalent, so it is a safe no-op in Phase 1;
// the sink still exists so the sink set is consistent across platforms.
class DebuggerSink final : public ILogSink
{
public:
    DebuggerSink();

    void write(const LogRecordView& record) noexcept override;
    void flush() noexcept override;

private:
    // Only read on Windows; kept on all platforms so the layout is uniform.
    [[maybe_unused]] bool active_;
    [[maybe_unused]] std::string scratch_;
};

} // namespace ludus::foundation::logging::internal
