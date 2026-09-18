#include "sinks/file_sink.hpp"

#include "internal/formatter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#    include <process.h>
#    define LUDUS_GETPID _getpid
#else
#    include <unistd.h>
#    define LUDUS_GETPID ::getpid
#endif

namespace ludus::foundation::logging::internal {

namespace {

// Build "YYYY-MM-DD_HH-MM-SS_pid-NNNNN.log" (spec section 20). Filesystem-safe
// on all target platforms (no colons).
std::string session_file_name()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm broken{};
#if defined(_WIN32)
    localtime_s(&broken, &now);
#else
    localtime_r(&now, &broken);
#endif

    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &broken);

    const auto pid = static_cast<long>(LUDUS_GETPID());
    return std::string(stamp) + "_pid-" + std::to_string(pid) + ".log";
}

// Delete oldest session files so at most `retained` remain (counting the file we
// are about to create). Matches on the "*_pid-*.log" session naming so unrelated
// files in the directory are left alone (spec section 22).
void prune_old_sessions(const std::filesystem::path& directory, std::uint32_t retained) noexcept
{
    if (retained == 0) {
        return;
    }
    std::error_code ec;
    std::vector<std::filesystem::directory_entry> sessions;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) {
            return;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.find("_pid-") != std::string::npos && name.size() >= 4 && name.substr(name.size() - 4) == ".log") {
            sessions.push_back(entry);
        }
    }

    // Keep (retained - 1) existing files so that adding the new one lands at
    // exactly `retained`. Sort by last-write time, oldest first.
    if (sessions.size() < retained) {
        return;
    }
    std::sort(sessions.begin(),
              sessions.end(),
              [](const std::filesystem::directory_entry& a, const std::filesystem::directory_entry& b) {
                  std::error_code ea;
                  std::error_code eb;
                  return a.last_write_time(ea) < b.last_write_time(eb);
              });

    const std::size_t keep = retained > 0 ? static_cast<std::size_t>(retained - 1) : 0;
    if (sessions.size() <= keep) {
        return;
    }
    const std::size_t to_remove = sessions.size() - keep;
    for (std::size_t i = 0; i < to_remove; ++i) {
        std::error_code remove_ec;
        std::filesystem::remove(sessions[i].path(), remove_ec);
    }
}

} // namespace

std::unique_ptr<FileSink> FileSink::create(const LogConfig& config)
{
    if (config.directory.empty()) {
        return nullptr;
    }

    std::error_code ec;
    std::filesystem::create_directories(config.directory, ec);
    if (ec) {
        return nullptr;
    }

    prune_old_sessions(config.directory, config.retained_sessions);

    const std::filesystem::path base_path = config.directory / session_file_name();
    std::FILE* file = std::fopen(base_path.string().c_str(), "wb");
    if (file == nullptr) {
        return nullptr;
    }

    return std::unique_ptr<FileSink>(new FileSink(file, config.directory, base_path, config.max_file_size_bytes));
}

FileSink::FileSink(std::FILE* file,
                   std::filesystem::path directory,
                   std::filesystem::path base_path,
                   std::uint64_t max_file_size_bytes)
    : file_(file), directory_(std::move(directory)), base_path_(std::move(base_path)), active_path_(base_path_),
      max_file_size_bytes_(max_file_size_bytes), bytes_written_(0), rotation_index_(0), scratch_()
{
    scratch_.reserve(256);
}

FileSink::~FileSink()
{
    if (file_ != nullptr) {
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
    }
}

void FileSink::rotate_if_needed(std::size_t incoming_bytes) noexcept
{
    if (max_file_size_bytes_ == 0 || file_ == nullptr) {
        return;
    }
    if (bytes_written_ + incoming_bytes <= max_file_size_bytes_) {
        return;
    }

    // Rotate: close the current file and open "<base>.<n>.log" (spec section 22).
    std::fflush(file_);
    std::fclose(file_);

    ++rotation_index_;

    // base_path_ ends in ".log"; insert ".<n>" before the extension.
    std::filesystem::path rotated = base_path_;
    rotated.replace_extension(); // drop ".log"
    std::filesystem::path next = rotated.string() + "." + std::to_string(rotation_index_) + ".log";

    std::FILE* new_file = std::fopen(next.string().c_str(), "wb");
    if (new_file == nullptr) {
        // Could not rotate; leave file_ null so we stop writing rather than
        // grow unbounded. A note would be nice but must not recurse into the
        // logging path here.
        file_ = nullptr;
        return;
    }
    file_ = new_file;
    active_path_ = std::move(next);
    bytes_written_ = 0;
}

void FileSink::write(const LogRecordView& record) noexcept
{
    if (file_ == nullptr) {
        return;
    }
    try {
        format_console_line(record, /*use_color=*/false, scratch_);
        scratch_.push_back('\n');
    }
    catch (...) {
        return;
    }

    rotate_if_needed(scratch_.size());
    if (file_ == nullptr) {
        return;
    }

    const std::size_t n = std::fwrite(scratch_.data(), 1, scratch_.size(), file_);
    bytes_written_ += n;
}

void FileSink::flush() noexcept
{
    if (file_ != nullptr) {
        std::fflush(file_);
    }
}

} // namespace ludus::foundation::logging::internal
