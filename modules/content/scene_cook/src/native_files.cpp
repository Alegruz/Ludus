#include "native_files.h"

#include <ludus/foundation/base/config.h>

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
constexpr usize MAX_NATIVE_PATH = 4096;

[[nodiscard]] bool CopyPath(std::string_view path, char (&buffer)[MAX_NATIVE_PATH + 1]) noexcept
{
    if (path.empty() || path.size() > MAX_NATIVE_PATH)
    {
        return false;
    }
    for (usize i = 0; i < path.size(); ++i)
    {
        if (path[i] == '\0')
        {
            return false;
        }
        buffer[i] = path[i];
    }
    buffer[path.size()] = '\0';
    return true;
}
} // namespace

Status ResolveDirectory(std::string_view path, foundation::String& output) noexcept
{
    char input[MAX_NATIVE_PATH + 1]{};
    if (!CopyPath(path, input))
    {
        return Status::Invalid;
    }
    char resolved[PATH_MAX + 1]{};
    if (realpath(input, resolved) == nullptr)
    {
        return errno == ENOENT || errno == ENOTDIR ? Status::NotFound : Status::IoError;
    }
    struct stat info
    {
    };
    if (stat(resolved, &info) != 0 || !S_ISDIR(info.st_mode))
    {
        return Status::NotFound;
    }
    return output.TryAssign(resolved) == foundation::StringStatus::Ok ? Status::Ok : Status::OutOfMemory;
}

Status MakeDirectories(std::string_view path) noexcept
{
    char buffer[MAX_NATIVE_PATH + 1]{};
    if (!CopyPath(path, buffer))
    {
        return Status::Invalid;
    }
    for (usize i = 1; i <= path.size(); ++i)
    {
        if (i != path.size() && buffer[i] != '/')
        {
            continue;
        }
        const char saved = buffer[i];
        buffer[i] = '\0';
        if (mkdir(buffer, 0777) != 0 && errno != EEXIST)
        {
            return Status::IoError;
        }
        buffer[i] = saved;
    }
    struct stat info
    {
    };
    return stat(buffer, &info) == 0 && S_ISDIR(info.st_mode) ? Status::Ok : Status::IoError;
}

Status WriteFileAtomic(std::string_view directory, std::string_view name, std::span<const uint8> data) noexcept
{
    char target[MAX_NATIVE_PATH + 1]{};
    char temporary[MAX_NATIVE_PATH + 1]{};
    constexpr std::string_view SUFFIX = ".ludus-cook-XXXXXX";
    if (directory.size() + 1 + name.size() + SUFFIX.size() > MAX_NATIVE_PATH)
    {
        return Status::Limit;
    }
    usize at = 0;
    for (const char value : directory)
    {
        target[at++] = value;
    }
    target[at++] = '/';
    for (const char value : name)
    {
        target[at++] = value;
    }
    for (usize i = 0; i < at; ++i)
    {
        temporary[i] = target[i];
    }
    for (usize i = 0; i < SUFFIX.size(); ++i)
    {
        temporary[at + i] = SUFFIX[i];
    }
    const int file = mkstemp(temporary);
    if (file < 0)
    {
        return Status::IoError;
    }
    usize written = 0;
    bool ok = true;
    while (ok && written < data.size())
    {
        const ssize_t count = write(file, data.data() + written, data.size() - written);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        ok = count > 0;
        written += ok ? static_cast<usize>(count) : 0;
    }
    // Match the reference converter's default-permission files.
    const mode_t mask = umask(0);
    (void)umask(mask);
    ok = ok && fchmod(file, static_cast<mode_t>(0666 & ~mask)) == 0;
    ok = ok && fsync(file) == 0;
    ok = close(file) == 0 && ok;
    if (!ok || rename(temporary, target) != 0)
    {
        (void)unlink(temporary);
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

std::string_view BaseName(std::string_view path) noexcept
{
    while (path.size() > 1 && path.back() == '/')
    {
        path.remove_suffix(1);
    }
    const usize slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}
} // namespace ludus::content::scene_cook::detail
