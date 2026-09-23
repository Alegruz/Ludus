#include "internal/backend.hpp"

#include "internal/log_record.hpp"

namespace ludus::foundation::logging::internal
{

void AsyncBackend::RenderAndWrite(const QueuedRecord& rec) noexcept
{
    // Reconstruct the sink view over the dequeued OWNED bytes. The message points
    // into the slot's own storage (valid for this call); category/file/thread
    // name are process-lifetime borrowed views (requirements R24/R36).
    LogRecordView view;
    view.Level = rec.Level;
    view.Category = LogCategory{rec.CategoryId, rec.CategoryName};
    view.Message = std::string_view(rec.Message, rec.MessageLen);
    view.File = rec.File;
    view.Function = std::string_view{};
    view.Line = rec.Line;
    view.ThreadName = rec.ThreadNameView();
    view.ThreadId = rec.ThreadId;
    view.NativeThreadId = rec.NativeThreadId;
    view.MonotonicTicks = rec.MonotonicTicks;
    view.Sequence = rec.Sequence;

    for (auto& sink : mSinks)
    {
        (void)sink->Write(view);
    }
    // Error and Fatal flush promptly on the worker so their visibility promise
    // holds even in async mode (requirements R54: preserve Error visibility).
    if (rec.Level >= LogLevel::Error)
    {
        for (auto& sink : mSinks)
        {
            (void)sink->Flush();
        }
    }
}

} // namespace ludus::foundation::logging::internal
