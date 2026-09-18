#pragma once

#include <ludus/foundation/logging/config.hpp>

#include "internal/sink.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace ludus::foundation::logging::internal {

// Persistent per-process file sink (spec sections 20-23). One session file per
// process, named to avoid collisions between concurrent Ludus processes; writes
// continuously (not just at shutdown); rotates within a session at the size cap;
// prunes old sessions beyond the retention count on startup.
class FileSink final : public ILogSink
{
public:
    // Factory: opens the session file under config.Directory. Returns nullptr if
    // the directory cannot be created or the file cannot be opened, so the
    // caller can degrade gracefully rather than crash (spec section 25 spirit).
    static std::unique_ptr<FileSink> create(const LogConfig& config);

    ~FileSink() override;

    void Write(const LogRecordView& record) noexcept override;
    void Flush() noexcept override;

    // Exposed for tests: the absolute path of the currently active file.
    [[nodiscard]] const std::filesystem::path& current_path() const noexcept
    {
        return mActivePath;
    }

private:
    FileSink(std::FILE* file,
             std::filesystem::path directory,
             std::filesystem::path base_path,
             std::uint64_t maxFileSizeBytes);

    void rotateIfNeeded(std::size_t incoming_bytes) noexcept;

    std::FILE* mFile;
    std::filesystem::path mDirectory;
    std::filesystem::path mBasePath; // first file of the session (no rotation suffix)
    std::filesystem::path mActivePath;
    std::uint64_t mMaxFileSizeBytes;
    std::uint64_t mBytesWritten;
    std::uint32_t mRotationIndex;
    std::string mScratch;
};

} // namespace ludus::foundation::logging::internal
