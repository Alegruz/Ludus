#pragma once

#include <ludus/foundation/base/types.h>

#include "internal/sink.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace ludus::foundation::logging::internal
{

// Private file-sink configuration. FoundationLogging's public headers never
// expose <filesystem>; the directory arrives as a borrowed UTF-8 view and is
// converted to a path here, below the public boundary (requirements R21, R47).
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
    // be created after bounded retries, so the caller degrades gracefully.
    static std::unique_ptr<FileSink> Create(const FileSinkConfig& config);

    ~FileSink() override;

    SinkStatus Write(const LogRecordView& record) noexcept override;
    SinkStatus Flush() noexcept override;
    SinkStatus FlushDurable() noexcept override;

    // Exposed for tests: the absolute path of the currently active file.
    [[nodiscard]] const std::filesystem::path& CurrentPath() const noexcept
    {
        return mActivePath;
    }

    [[nodiscard]] bool Healthy() const noexcept
    {
        return mHealthy;
    }

private:
    FileSink(std::FILE* file,
             std::filesystem::path directory,
             std::filesystem::path base_path,
             const FileSinkConfig& config);

    // Rotate to the next numbered segment when the incoming write would exceed
    // the per-segment cap. On failure marks the sink unhealthy and stops file
    // output rather than silently exceeding the budget.
    void rotateIfNeeded(usize incoming_bytes) noexcept;

    std::FILE* mFile = nullptr;
    std::filesystem::path mDirectory;
    std::filesystem::path mBasePath; // first segment of the session
    std::filesystem::path mActivePath;
    uint64 mMaxFileSizeBytes = 0;
    uint64 mBytesWritten = 0;        // bytes in the current segment
    uint64 mSessionBytesWritten = 0; // bytes across all segments this session
    uint32 mRotationIndex = 0;
    bool mHealthy = true;
    std::string mScratch;
};

} // namespace ludus::foundation::logging::internal
