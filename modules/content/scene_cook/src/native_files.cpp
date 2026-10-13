#include "native_files.h"

#include <ludus/foundation/base/config.h>
#include <ludus/foundation/strings/static_string.hpp>

#include <atomic>

#if LUDUS_TARGET_OS == LUDUS_OS_LINUX || LUDUS_TARGET_OS == LUDUS_OS_MACOS
#    include <cerrno>
#    include <climits>
#    include <cstdio>
#    include <cstdlib>
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#    define LUDUS_SCENE_COOK_POSIX 1
#else
#    define LUDUS_SCENE_COOK_POSIX 0
#endif

namespace ludus::content::scene_cook::detail
{
#if LUDUS_SCENE_COOK_POSIX
namespace
{
constexpr usize MAX_NATIVE_PATH_BYTES = 4096;
/// Bounded, always NUL-terminated native path; overflow is reported, never truncated.
using NativePath = foundation::StaticString<MAX_NATIVE_PATH_BYTES>;
/// Attempts at a fresh temporary name before reporting an I/O error.
constexpr uint32 MAX_TEMPORARY_ATTEMPTS = 64;

std::atomic<uint32> gTemporaryCounter{0};

/// Copy path into a NativePath; rejects empty paths and embedded NUL bytes.
[[nodiscard]] bool TryMakeNativePath(std::string_view path, NativePath& outPath) noexcept
{
    if (path.empty() == true || path.find('\0') != std::string_view::npos)
    {
        return false;
    }
    return outPath.TryAssign(path) == foundation::StringStatus::Ok;
}

/// stat(2) the path and report whether it names a directory.
[[nodiscard]] bool IsDirectory(const char* path) noexcept
{
    struct stat fileStatus = {};
    return stat(path, &fileStatus) == 0 && S_ISDIR(fileStatus.st_mode);
}

/// Append the decimal digits of value.
[[nodiscard]] bool TryAppendDecimal(NativePath& inoutPath, uint64 value) noexcept
{
    char digits[20]{};
    usize count = 0;
    do
    {
        digits[sizeof(digits) - 1 - count] = static_cast<char>('0' + value % 10);
        value /= 10;
        ++count;
    } while (value != 0);
    return inoutPath.TryAppend(std::string_view(digits + sizeof(digits) - count, count)) ==
           foundation::StringStatus::Ok;
}
} // namespace

Status ResolveDirectory(std::string_view path, foundation::String& outPath) noexcept
{
    NativePath input;
    if (TryMakeNativePath(path, input) == false)
    {
        return Status::Invalid;
    }
    // realpath(3) writes into a caller buffer of at least PATH_MAX bytes; the
    // result is copied into the engine String straight away.
    char resolved[PATH_MAX + 1]{};
    if (realpath(input.GetData(), resolved) == nullptr)
    {
        return errno == ENOENT || errno == ENOTDIR ? Status::NotFound : Status::IoError;
    }
    if (IsDirectory(resolved) == false)
    {
        return Status::NotFound;
    }
    return outPath.TryAssign(resolved) == foundation::StringStatus::Ok ? Status::Ok : Status::OutOfMemory;
}

Status MakeDirectories(std::string_view path) noexcept
{
    NativePath full;
    if (TryMakeNativePath(path, full) == false)
    {
        return Status::Invalid;
    }
    NativePath prefix;
    for (usize i = 1; i <= path.size(); ++i)
    {
        if (i != path.size() && path[i] != '/')
        {
            continue;
        }
        if (prefix.TryAssign(path.substr(0, i)) != foundation::StringStatus::Ok)
        {
            return Status::Invalid;
        }
        if (mkdir(prefix.GetData(), 0777) != 0 && errno != EEXIST)
        {
            return Status::IoError;
        }
    }
    return IsDirectory(full.GetData()) == true ? Status::Ok : Status::IoError;
}

Status WriteFileAtomic(std::string_view directory, std::string_view name, std::span<const uint8> data) noexcept
{
    NativePath target;
    if (target.TryAssign(directory) != foundation::StringStatus::Ok ||
        target.TryAppend("/") != foundation::StringStatus::Ok || target.TryAppend(name) != foundation::StringStatus::Ok)
    {
        return Status::Limit;
    }
    // Create the temporary with mode 0666 and O_EXCL so the kernel applies the
    // process umask itself, as the reference converter's open() did. The umask
    // is never read or changed, so concurrent threads are unaffected.
    NativePath temporary;
    int file = -1;
    for (uint32 attempt = 0; attempt < MAX_TEMPORARY_ATTEMPTS && file < 0; ++attempt)
    {
        temporary.Clear();
        const bool isNamed =
            temporary.TryAssign(target.GetView()) == foundation::StringStatus::Ok &&
            temporary.TryAppend(".ludus-cook-") == foundation::StringStatus::Ok &&
            TryAppendDecimal(temporary, static_cast<uint64>(getpid())) == true &&
            temporary.TryAppend("-") == foundation::StringStatus::Ok &&
            TryAppendDecimal(temporary, gTemporaryCounter.fetch_add(1, std::memory_order_relaxed)) == true;
        if (isNamed == false)
        {
            return Status::Limit;
        }
        file = open(temporary.GetData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
        if (file < 0 && errno != EEXIST && errno != EINTR)
        {
            return Status::IoError;
        }
    }
    if (file < 0)
    {
        return Status::IoError;
    }
    usize written = 0;
    bool isOk = true;
    while (isOk == true && written < data.size())
    {
        const ssize_t count = write(file, data.data() + written, data.size() - written);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        isOk = count > 0;
        written += isOk == true ? static_cast<usize>(count) : 0;
    }
    isOk = isOk == true && fsync(file) == 0;
    isOk = close(file) == 0 && isOk == true;
    if (isOk == false || rename(temporary.GetData(), target.GetData()) != 0)
    {
        (void)unlink(temporary.GetData());
        return Status::IoError;
    }
    return Status::Ok;
}
#else
Status ResolveDirectory(std::string_view, foundation::String&) noexcept
{
    return Status::Unsupported;
}
Status MakeDirectories(std::string_view) noexcept
{
    return Status::Unsupported;
}
Status WriteFileAtomic(std::string_view, std::string_view, std::span<const uint8>) noexcept
{
    return Status::Unsupported;
}
#endif

std::string_view GetBaseName(std::string_view path) noexcept
{
    while (path.size() > 1 && path.back() == '/')
    {
        path.remove_suffix(1);
    }
    const usize slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}
} // namespace ludus::content::scene_cook::detail
