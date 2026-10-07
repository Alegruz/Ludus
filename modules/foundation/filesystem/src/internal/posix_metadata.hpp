#pragma once

#include <ludus/foundation/filesystem/filesystem.hpp>

#include <sys/stat.h>

namespace ludus::foundation::filesystem::detail
{
inline FileStamp Stamp(const struct stat& info) noexcept
{
#if defined(LUDUS_PLATFORM_MACOS)
    const auto modified = info.st_mtimespec;
    const auto changed = info.st_ctimespec;
#else
    const auto modified = info.st_mtim;
    const auto changed = info.st_ctim;
#endif
    return {static_cast<uint64>(info.st_dev),
            static_cast<uint64>(info.st_ino),
            static_cast<uint64>(info.st_size),
            static_cast<int64>(modified.tv_sec),
            static_cast<int64>(modified.tv_nsec),
            static_cast<int64>(changed.tv_sec),
            static_cast<int64>(changed.tv_nsec)};
}
} // namespace ludus::foundation::filesystem::detail
