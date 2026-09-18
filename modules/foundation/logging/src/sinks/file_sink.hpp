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
    // Factory: opens the session file under config.directory. Returns nullptr if
    // the directory cannot be created or the file cannot be opened, so the
    // caller can degrade gracefully rather than crash (spec section 25 spirit).
    static std::unique_ptr<FileSink> create(const LogConfig& config);

    ~FileSink() override;

    void write(const LogRecordView& record) noexcept override;
    void flush() noexcept override;

    // Exposed for tests: the absolute path of the currently active file.
    [[nodiscard]] const std::filesystem::path& current_path() const noexcept
    {
        return active_path_;
    }

private:
    FileSink(std::FILE* file,
             std::filesystem::path directory,
             std::filesystem::path base_path,
             std::uint64_t max_file_size_bytes);

    void rotate_if_needed(std::size_t incoming_bytes) noexcept;

    std::FILE* file_;
    std::filesystem::path directory_;
    std::filesystem::path base_path_; // first file of the session (no rotation suffix)
    std::filesystem::path active_path_;
    std::uint64_t max_file_size_bytes_;
    std::uint64_t bytes_written_;
    std::uint32_t rotation_index_;
    std::string scratch_;
};

} // namespace ludus::foundation::logging::internal
