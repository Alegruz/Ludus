#pragma once

#include <ludus/foundation/base/types.h>

#include "internal/sink.hpp"

#include <string_view>

// This private header is kept free of <filesystem>/<memory>/<string> so it stays
// within the build-time budget: those headers (especially <filesystem>, and
// <memory> which drags in <format> on libstdc++) are heavy, and file_sink.hpp is
// included by more than one TU. All the filesystem/session state lives behind a
// PIMPL in file_sink.cpp (ADR 0004/0005; requirements R21).

namespace ludus::foundation::logging::internal
{

// Private file-sink configuration. FoundationLogging's public headers never
// expose <filesystem>; the directory arrives as a borrowed UTF-8 view and is
// converted to a path in the .cpp, below the public boundary (requirements
// R21, R47).
struct FileSinkConfig
{
    std::string_view Directory;
    uint64 MaxFileSizeBytes = 32ull * 1024ull * 1024ull; // 0 = no per-segment cap
    uint64 MaxTotalBytes = 0;                            // 0 = no total cap
    uint32 MaxSessionAgeDays = 0;                        // 0 = no age cap
    uint32 RetainedSessions = 10;                        // 0 = keep all
};

// Persistent per-process file sink (design.md section 9). One session per
// process created by EXCLUSIVE creation (never truncates an existing file,
// fixing F7); writes continuously; rotates within a session at the size cap;
// prunes only whole inactive sessions this logger created (never active or
// unrelated files, fixing F7); reports write/rotation failures via SinkStatus
// and marks itself unhealthy rather than silently disabling (fixing F8).
class FileSink final : public ILogSink
{
public:
    // Opens a fresh, exclusively-created session file under config.Directory.
    // Returns nullptr if the directory cannot be created or no unique file could
    // be created after bounded retries, so the caller degrades gracefully. The
    // caller owns the returned pointer (wraps it in a unique_ptr).
    static FileSink* Create(const FileSinkConfig& config);

    ~FileSink() override;

    SinkStatus Write(const LogRecordView& record) noexcept override;
    SinkStatus Flush() noexcept override;
    SinkStatus FlushDurable() noexcept override;

    // Exposed for tests: the absolute path of the currently active file, as a
    // UTF-8 string (avoids exposing std::filesystem::path in the header).
    [[nodiscard]] std::string_view CurrentPathUtf8() const noexcept;

    [[nodiscard]] bool Healthy() const noexcept;

private:
    struct Impl;
    explicit FileSink(Impl* impl) noexcept : mImpl(impl) {}

    Impl* mImpl = nullptr; // owned; freed in the destructor
};

} // namespace ludus::foundation::logging::internal
