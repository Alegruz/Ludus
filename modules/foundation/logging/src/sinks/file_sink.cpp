#include "sinks/file_sink.hpp"

#include "internal/formatter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <format>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#    include <io.h>
#    include <process.h>
#    define LUDUS_GETPID _getpid
#else
#    include <unistd.h>
#    define LUDUS_GETPID ::getpid
#endif

namespace ludus::foundation::logging::internal
{

namespace
{

// Process-wide nonce so two session files created in the same second by the same
// PID (rapid reinit) never collide on a name (requirements R42; fixes F7).
std::atomic<uint32> gSessionNonce{0};

// Build "YYYY-MM-DD_HH-MM-SS_pid-NNNNN_nMM.log" using UTC time. UTC avoids
// ambiguity across DST/zone changes; the nonce + exclusive creation are the
// real collision guards.
std::string sessionFileName(uint32 nonce)
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm broken{};
#if defined(_WIN32)
    gmtime_s(&broken, &now);
#else
    gmtime_r(&now, &broken);
#endif

    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &broken);

    const auto pid = static_cast<long>(LUDUS_GETPID());
    return std::format("{}_pid-{}_n{}.log", stamp, pid, nonce);
}

// The base name of a session file WITHOUT the rotation suffix, used to group all
// segments of one session together. Session files this logger creates end in
// "_pid-<pid>_n<nonce>.log" (segment 0) or ".<k>.log" after the base for k>0.

// Retention limits grouped so the pruning API is not a row of same-typed args.
struct RetentionLimits
{
    uint32 RetainedSessions = 0;  // 0 = keep all
    uint64 MaxTotalBytes = 0;     // 0 = no total cap
    uint32 MaxSessionAgeDays = 0; // 0 = no age cap
};

// Extract the session group key from a filename. Returns empty if the file is
// not one this logger created (narrow ownership check; never touches unrelated
// files, requirements R43; fixes F7).
std::string sessionBaseKey(const std::string& filename)
{
    // Must end in ".log".
    if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".log")
    {
        return {};
    }
    // Must contain the "_pid-...._n<nonce>" session marker this logger emits.
    const auto pid_pos = filename.find("_pid-");
    const auto nonce_pos = filename.find("_n");
    if (pid_pos == std::string::npos || nonce_pos == std::string::npos || nonce_pos < pid_pos)
    {
        return {};
    }
    // Strip a trailing ".<k>" rotation suffix (before ".log") so all segments
    // of one session share a key. Base segment key is the name minus ".log".
    std::string stem = filename.substr(0, filename.size() - 4); // drop ".log"
    // If stem ends in ".<digits>" AND the part before also matches the session
    // shape, treat the ".<digits>" as a rotation index.
    const auto dot = stem.rfind('.');
    if (dot != std::string::npos && dot > nonce_pos)
    {
        bool all_digits = dot + 1 < stem.size();
        for (usize i = dot + 1; i < stem.size(); ++i)
        {
            if (stem[i] < '0' || stem[i] > '9')
            {
                all_digits = false;
                break;
            }
        }
        if (all_digits)
        {
            stem = stem.substr(0, dot);
        }
    }
    return stem;
}

// Prune whole inactive session groups so at most `retained` sessions (including
// the one about to be created) remain, and (optionally) enforce a total-byte cap
// and age cap. Never deletes the active session or unrelated files. Metadata is
// gathered BEFORE sorting; the comparator is a strict weak ordering over an
// immutable snapshot (requirements R43/R44; fixes F7/F9).
void pruneSessions(const std::filesystem::path& directory,
                   std::string_view activeKey,
                   const RetentionLimits& limits) noexcept
{
    const uint32 retained = limits.RetainedSessions;
    const uint64 maxTotalBytes = limits.MaxTotalBytes;
    const uint32 maxAgeDays = limits.MaxSessionAgeDays;

    std::error_code ec;
    std::filesystem::directory_iterator iter(directory, ec);
    if (ec)
    {
        return;
    }

    const std::string_view active_key = activeKey;

    // Group segments by session key; collect immutable metadata up front.
    struct Group
    {
        std::string Key;
        std::vector<std::filesystem::path> Segments;
        std::filesystem::file_time_type NewestTime;
        bool TimeValid = false;
        uint64 TotalBytes = 0;
        bool IsActive = false;
    };
    std::vector<Group> groups;

    std::filesystem::directory_iterator end;
    for (; iter != end; iter.increment(ec))
    {
        if (ec)
        {
            break;
        }
        const std::filesystem::directory_entry& entry = *iter;
        std::error_code fec;
        if (!entry.is_regular_file(fec) || fec)
        {
            continue;
        }
        const std::string name = entry.path().filename().string();
        const std::string key = sessionBaseKey(name);
        if (key.empty())
        {
            continue; // not our file — leave it alone
        }

        std::error_code tec;
        const auto wtime = entry.last_write_time(tec);
        std::error_code sec;
        const auto size = std::filesystem::file_size(entry.path(), sec);

        Group* group = nullptr;
        for (auto& g : groups)
        {
            if (g.Key == key)
            {
                group = &g;
                break;
            }
        }
        if (group == nullptr)
        {
            groups.push_back(Group{key, {}, {}, false, 0, key == active_key});
            group = &groups.back();
        }
        group->Segments.push_back(entry.path());
        if (!sec)
        {
            group->TotalBytes += size;
        }
        if (!tec && (!group->TimeValid || wtime > group->NewestTime))
        {
            group->NewestTime = wtime;
            group->TimeValid = true;
        }
    }

    // Age cap: delete inactive groups older than maxAgeDays.
    if (maxAgeDays > 0)
    {
        const auto now = std::filesystem::file_time_type::clock::now();
        const auto max_age = std::chrono::hours(24) * static_cast<int>(maxAgeDays);
        for (auto& g : groups)
        {
            if (g.IsActive || !g.TimeValid)
            {
                continue;
            }
            if (now - g.NewestTime > max_age)
            {
                for (const auto& seg : g.Segments)
                {
                    std::error_code rec;
                    std::filesystem::remove(seg, rec);
                }
                g.Segments.clear();
            }
        }
    }

    // Build the list of surviving inactive groups, sorted oldest-first over an
    // immutable snapshot (strict weak ordering; entries without a valid time
    // sort last deterministically).
    std::vector<Group*> inactive;
    for (auto& g : groups)
    {
        if (!g.IsActive && !g.Segments.empty())
        {
            inactive.push_back(&g);
        }
    }
    std::sort(inactive.begin(), inactive.end(), [](const Group* a, const Group* b) {
        if (a->TimeValid != b->TimeValid)
        {
            return a->TimeValid; // valid times sort before invalid
        }
        if (!a->TimeValid)
        {
            return a->Key < b->Key; // stable tiebreak
        }
        if (a->NewestTime != b->NewestTime)
        {
            return a->NewestTime < b->NewestTime;
        }
        return a->Key < b->Key;
    });

    // Session-count cap: keep (retained - 1) most-recent inactive groups so the
    // new active session lands at exactly `retained`.
    usize active_count = 0;
    for (const auto& g : groups)
    {
        if (g.IsActive && !g.Segments.empty())
        {
            active_count = 1;
        }
    }
    if (retained > 0)
    {
        const usize keep_inactive = retained > active_count ? (retained - active_count) : 0;
        if (inactive.size() > keep_inactive)
        {
            const usize remove_count = inactive.size() - keep_inactive;
            for (usize i = 0; i < remove_count; ++i)
            {
                for (const auto& seg : inactive[i]->Segments)
                {
                    std::error_code rec;
                    std::filesystem::remove(seg, rec);
                }
                inactive[i]->Segments.clear();
            }
        }
    }

    // Total-byte cap: remove oldest inactive groups until under the cap.
    if (maxTotalBytes > 0)
    {
        uint64 total = 0;
        for (const auto& g : groups)
        {
            total += g.TotalBytes;
        }
        for (usize i = 0; i < inactive.size() && total > maxTotalBytes; ++i)
        {
            if (inactive[i]->Segments.empty())
            {
                continue;
            }
            for (const auto& seg : inactive[i]->Segments)
            {
                std::error_code rec;
                std::filesystem::remove(seg, rec);
            }
            total = total > inactive[i]->TotalBytes ? total - inactive[i]->TotalBytes : 0;
            inactive[i]->Segments.clear();
        }
    }
}

// Exclusive create: "wbx" fails if the file already exists (C11), so we never
// truncate someone else's or a prior same-name session (requirements R42; F7).
std::FILE* exclusiveCreate(const std::filesystem::path& path) noexcept
{
#if defined(_WIN32)
    std::FILE* file = nullptr;
    // "wbx" is honored by the UCRT; fall back if unavailable.
    if (::fopen_s(&file, path.string().c_str(), "wbx") != 0)
    {
        return nullptr;
    }
    return file;
#else
    return std::fopen(path.string().c_str(), "wbx");
#endif
}

} // namespace

std::unique_ptr<FileSink> FileSink::Create(const FileSinkConfig& config)
{
    if (config.Directory.empty())
    {
        return nullptr;
    }

    const std::filesystem::path directory{std::string(config.Directory)};

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec && !std::filesystem::exists(directory))
    {
        return nullptr;
    }

    // Create the session file by exclusive creation with bounded retries on a
    // fresh nonce. Exclusive creation — not a merely-unique-looking name — is
    // the correctness mechanism.
    std::filesystem::path base_path;
    std::FILE* file = nullptr;
    for (int attempt = 0; attempt < 64 && file == nullptr; ++attempt)
    {
        const uint32 nonce = gSessionNonce.fetch_add(1, std::memory_order_relaxed);
        base_path = directory / sessionFileName(nonce);
        file = exclusiveCreate(base_path);
    }
    if (file == nullptr)
    {
        return nullptr;
    }

    // Prune old sessions AFTER creating the active file so it is protected as
    // the active session (never pruned as "old").
    const RetentionLimits limits{config.RetainedSessions, config.MaxTotalBytes, config.MaxSessionAgeDays};
    const std::string active_key = sessionBaseKey(base_path.filename().string());
    pruneSessions(directory, active_key, limits);

    return std::unique_ptr<FileSink>(new FileSink(file, directory, base_path, config));
}

FileSink::FileSink(std::FILE* file,
                   std::filesystem::path directory,
                   std::filesystem::path base_path,
                   const FileSinkConfig& config)
    : mFile(file), mDirectory(std::move(directory)), mBasePath(std::move(base_path)), mActivePath(mBasePath),
      mMaxFileSizeBytes(config.MaxFileSizeBytes)
{
    mScratch.reserve(256);
}

FileSink::~FileSink()
{
    if (mFile != nullptr)
    {
        std::fflush(mFile);
        std::fclose(mFile);
        mFile = nullptr;
    }
}

void FileSink::rotateIfNeeded(usize incoming_bytes) noexcept
{
    if (mMaxFileSizeBytes == 0 || mFile == nullptr)
    {
        return;
    }
    if (mBytesWritten + incoming_bytes <= mMaxFileSizeBytes)
    {
        return;
    }

    ++mRotationIndex;
    // mBasePath ends in ".log"; insert ".<n>" before the extension.
    std::filesystem::path rotated = mBasePath;
    rotated.replace_extension(); // drop ".log"
    const std::filesystem::path next = std::format("{}.{}.log", rotated.string(), mRotationIndex);

    // Open the replacement BEFORE relinquishing the old handle so a failed
    // rotation leaves the current segment usable (requirements R44).
    std::FILE* new_file = exclusiveCreate(next);
    if (new_file == nullptr)
    {
        // Could not rotate. Keep writing to the current segment rather than
        // silently disabling output; mark unhealthy so callers can observe it.
        mHealthy = false;
        return;
    }

    std::fflush(mFile);
    std::fclose(mFile);
    mFile = new_file;
    mActivePath = next;
    mBytesWritten = 0;
}

SinkStatus FileSink::Write(const LogRecordView& record) noexcept
{
    if (mFile == nullptr)
    {
        return SinkStatus::Failed;
    }
    FormatConsoleLine(record, /*use_color=*/false, mScratch);
    mScratch.push_back('\n');

    rotateIfNeeded(mScratch.size());
    if (mFile == nullptr)
    {
        mHealthy = false;
        return SinkStatus::Failed;
    }

    const usize n = std::fwrite(mScratch.data(), 1, mScratch.size(), mFile);
    mBytesWritten += n;
    mSessionBytesWritten += n;
    if (n != mScratch.size())
    {
        // Short write (e.g. disk full): mark unhealthy and report failure so the
        // record is not counted as delivered (requirements R45; fixes F8).
        mHealthy = false;
        return SinkStatus::Failed;
    }
    return SinkStatus::Ok;
}

SinkStatus FileSink::Flush() noexcept
{
    if (mFile == nullptr)
    {
        return SinkStatus::Failed;
    }
    return std::fflush(mFile) == 0 ? SinkStatus::Ok : SinkStatus::Failed;
}

SinkStatus FileSink::FlushDurable() noexcept
{
    if (mFile == nullptr)
    {
        return SinkStatus::Failed;
    }
    if (std::fflush(mFile) != 0)
    {
        return SinkStatus::Failed;
    }
    // fflush drains C-library buffering only; a durable flush needs an OS sync
    // of the file's storage (requirements R46). This can stall significantly and
    // is only issued on an explicit Durable flush request.
#if defined(_WIN32)
    return SinkStatus::Ok; // FlushFileBuffers wiring is a platform-adapter task
#else
    const int fd = ::fileno(mFile);
    if (fd < 0)
    {
        return SinkStatus::Failed;
    }
    return ::fdatasync(fd) == 0 ? SinkStatus::Ok : SinkStatus::Failed;
#endif
}

} // namespace ludus::foundation::logging::internal
