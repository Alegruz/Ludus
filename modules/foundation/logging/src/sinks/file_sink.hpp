#pragma once

#include <ludus/foundation/base/types.h>

#include "internal/sink.hpp"

#include <cstdio>
#include <string>
#include <string_view>

// This private header stores its path state as std::string (UTF-8) rather than
// std::filesystem::path, so it needs neither <filesystem> nor <memory> (the
// latter drags in <format> on libstdc++). The actual filesystem operations use
// std::filesystem inside file_sink.cpp, below this boundary. This keeps the
// header a plain, readable class (no PIMPL) that stays within the build-time
// budget (ADR 0004/0005; requirements R21). Paths are already round-tripped to
// UTF-8 for fopen, so std::string members change no behavior.

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
    // caller owns the returned pointer (logger.cpp wraps it in a unique_ptr);
    // a raw owning pointer keeps <memory> out of this header.
    static FileSink* Create(const FileSinkConfig& config);

    ~FileSink() override;

    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;
    FileSink(FileSink&&) = delete;
    FileSink& operator=(FileSink&&) = delete;

    SinkStatus Write(const LogRecordView& record) noexcept override;
    SinkStatus Flush() noexcept override;
    SinkStatus FlushDurable() noexcept override;

    // Exposed for tests: the absolute path of the currently active file, as a
    // UTF-8 string (avoids exposing std::filesystem::path in the header).
    [[nodiscard]] std::string_view CurrentPathUtf8() const noexcept
    {
        return mActivePathUtf8;
    }

    [[nodiscard]] bool Healthy() const noexcept
    {
        return mHealthy;
    }

private:
    // Constructed by Create() with an already-open file handle and the UTF-8
    // paths of the session's first segment.
    FileSink(std::FILE* file, std::string directoryUtf8, std::string basePathUtf8, const FileSinkConfig& config);

    // Rotate to the next numbered segment when the incoming write would exceed
    // the per-segment cap. On failure marks the sink unhealthy and stops file
    // output rather than silently exceeding the budget.
    void RotateIfNeeded(usize incomingBytes) noexcept;

    std::FILE* mFile = nullptr;
    std::string mDirectoryUtf8;
    std::string mBasePathUtf8; // first segment of the session (no rotation suffix)
    std::string mActivePathUtf8;
    uint64 mMaxFileSizeBytes = 0;
    uint64 mBytesWritten = 0;        // bytes in the current segment
    uint64 mSessionBytesWritten = 0; // bytes across all segments this session
    uint32 mRotationIndex = 0;
    bool mHealthy = true;
    std::string mScratch;
};

} // namespace ludus::foundation::logging::internal
