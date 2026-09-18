#include "sinks/file_sink.hpp"

#include "internal/formatter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <format>
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
std::string sessionFileName()
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
    return std::format("{}_pid-{}.log", stamp, pid);
}

// Delete oldest session files so at most `retained` remain (counting the file we
// are about to create). Matches on the "*_pid-*.log" session naming so unrelated
// files in the directory are left alone (spec section 22).
void pruneOldSessions(const std::filesystem::path& directory, std::uint32_t retained) noexcept
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
    if (config.Directory.empty()) {
        return nullptr;
    }

    std::error_code ec;
    std::filesystem::create_directories(config.Directory, ec);
    if (ec) {
        return nullptr;
    }

    pruneOldSessions(config.Directory, config.RetainedSessions);

    const std::filesystem::path base_path = config.Directory / sessionFileName();
    std::FILE* file = std::fopen(base_path.string().c_str(), "wb");
    if (file == nullptr) {
        return nullptr;
    }

    return std::unique_ptr<FileSink>(new FileSink(file, config.Directory, base_path, config.MaxFileSizeBytes));
}

FileSink::FileSink(std::FILE* file,
                   std::filesystem::path directory,
                   std::filesystem::path base_path,
                   std::uint64_t maxFileSizeBytes)
    : mFile(file), mDirectory(std::move(directory)), mBasePath(std::move(base_path)), mActivePath(mBasePath),
      mMaxFileSizeBytes(maxFileSizeBytes), mBytesWritten(0), mRotationIndex(0), mScratch()
{
    mScratch.reserve(256);
}

FileSink::~FileSink()
{
    if (mFile != nullptr) {
        std::fflush(mFile);
        std::fclose(mFile);
        mFile = nullptr;
    }
}

void FileSink::rotateIfNeeded(std::size_t incoming_bytes) noexcept
{
    if (mMaxFileSizeBytes == 0 || mFile == nullptr) {
        return;
    }
    if (mBytesWritten + incoming_bytes <= mMaxFileSizeBytes) {
        return;
    }

    // Rotate: close the current file and open "<base>.<n>.log" (spec section 22).
    std::fflush(mFile);
    std::fclose(mFile);

    ++mRotationIndex;

    // mBasePath ends in ".log"; insert ".<n>" before the extension.
    std::filesystem::path rotated = mBasePath;
    rotated.replace_extension(); // drop ".log"
    std::filesystem::path next = std::format("{}.{}.log", rotated.string(), mRotationIndex);

    std::FILE* new_file = std::fopen(next.string().c_str(), "wb");
    if (new_file == nullptr) {
        // Could not rotate; leave mFile null so we stop writing rather than
        // grow unbounded. A note would be nice but must not recurse into the
        // logging path here.
        mFile = nullptr;
        return;
    }
    mFile = new_file;
    mActivePath = std::move(next);
    mBytesWritten = 0;
}

void FileSink::Write(const LogRecordView& record) noexcept
{
    if (mFile == nullptr) {
        return;
    }
    try {
        FormatConsoleLine(record, /*use_color=*/false, mScratch);
        mScratch.push_back('\n');
    }
    catch (...) {
        return;
    }

    rotateIfNeeded(mScratch.size());
    if (mFile == nullptr) {
        return;
    }

    const std::size_t n = std::fwrite(mScratch.data(), 1, mScratch.size(), mFile);
    mBytesWritten += n;
}

void FileSink::Flush() noexcept
{
    if (mFile != nullptr) {
        std::fflush(mFile);
    }
}

} // namespace ludus::foundation::logging::internal
