#include <ludus/content/content.h>
#include <ludus/foundation/base/checked_integer.hpp>
#include <ludus/foundation/base/config.h>
#include <ludus/foundation/filesystem/filesystem.hpp>
#include <new>

#if LUDUS_TARGET_OS == LUDUS_OS_LINUX
#    include <cerrno>
#    include <fcntl.h>
#    include <sys/file.h>
#    include <sys/stat.h>
#    include <sys/syscall.h>
#    include <unistd.h>
#endif

namespace ludus::content
{
#if LUDUS_TARGET_OS == LUDUS_OS_LINUX
namespace
{
struct Fd final
{
    int Value = -1;
    ~Fd() noexcept
    {
        if (Value >= 0)
        {
            close(Value);
        }
    }
};
Status Parent(std::string_view root, std::string_view path, Fd& directory, Text<1024>& leaf) noexcept
{
    if (root.empty() || root.size() > 4096 || root.find('\0') != std::string_view::npos || !ValidPath(path))
    {
        return Status::Invalid;
    }
    Text<4096> name;
    (void)name.Set(root);
    directory.Value = open(name.Data, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory.Value < 0)
    {
        return Status::IoError;
    }
    usize begin = 0;
    while (true)
    {
        const auto slash = path.find('/', begin);
        if (slash == std::string_view::npos)
        {
            (void)leaf.Set(path.substr(begin));
            return Status::Ok;
        }
        Text<1024> segment;
        (void)segment.Set(path.substr(begin, slash - begin));
        const int next = openat(directory.Value, segment.Data, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0)
        {
            return Status::IoError;
        }
        close(directory.Value);
        directory.Value = next;
        begin = slash + 1;
    }
}
Status ReadAt(int directory, const char* path, usize cap, Bytes& output) noexcept
{
    Fd fd;
    fd.Value = openat(directory, path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd.Value < 0)
    {
        return errno == ENOENT ? Status::NotFound : Status::IoError;
    }
    struct stat info
    {
    };
    if (fstat(fd.Value, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0)
    {
        return Status::IoError;
    }
    usize size{};
    if (!ludus::foundation::TryIntegerCast(info.st_size, size) || size > cap)
    {
        return Status::Limit;
    }
    Bytes next;
    if (!next.Resize(size))
    {
        return Status::OutOfMemory;
    }
    usize offset = 0;
    while (offset < size)
    {
        const auto count = read(fd.Value, next.Data().data() + offset, size - offset);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            return Status::IoError;
        }
        offset += static_cast<usize>(count);
    }
    uint8 extra = 0;
    if (read(fd.Value, &extra, 1) != 0)
    {
        return Status::Conflict;
    }
    output = Move(next);
    return Status::Ok;
}
} // namespace
#endif
namespace
{
Status ContentStatus(foundation::filesystem::Status status) noexcept
{
    using FileStatus = foundation::filesystem::Status;
    switch (status)
    {
        case FileStatus::Ok:
            return Status::Ok;
        case FileStatus::CorruptData:
        case FileStatus::InvalidArgument:
            return Status::Invalid;
        case FileStatus::NotFound:
            return Status::NotFound;
        case FileStatus::LimitExceeded:
            return Status::Limit;
        case FileStatus::OutOfMemory:
            return Status::OutOfMemory;
        case FileStatus::Changed:
            return Status::Conflict;
        case FileStatus::Unsupported:
            return Status::Unsupported;
        case FileStatus::AccessDenied:
        case FileStatus::NotRegularFile:
        case FileStatus::IoError:
            return Status::IoError;
    }
    return Status::IoError;
}
} // namespace
Status ReadFile(std::string_view root, std::string_view path, usize cap, Bytes& output) noexcept
{
    if (root.empty() || !ValidPath(path))
    {
        return Status::Invalid;
    }
    foundation::filesystem::Directory directory;
    auto result = directory.Open(root);
    if (!result.Succeeded())
    {
        return ContentStatus(result.Code);
    }
    foundation::filesystem::File file;
    result = directory.OpenRead(path, file);
    if (!result.Succeeded())
    {
        return ContentStatus(result.Code);
    }
    if (file.Size() > cap)
    {
        return Status::Limit;
    }
    Bytes next;
    usize size = 0;
    if (!foundation::TryIntegerCast(file.Size(), size))
    {
        return Status::Limit;
    }
    if (!next.Resize(size))
    {
        return Status::OutOfMemory;
    }
    const auto read = file.ReadAt(0, next.Data());
    if (!read.Outcome.Succeeded())
    {
        return ContentStatus(read.Outcome.Code);
    }
    output = Move(next);
    return Status::Ok;
}
Status
SaveFile(std::string_view root, std::string_view path, std::span<const uint8> data, const Digest* expected) noexcept
{
    if (root.empty() || !ValidPath(path))
    {
        return Status::Invalid;
    }
#if LUDUS_TARGET_OS == LUDUS_OS_LINUX
    if (data.size() > usize{32} * 1024 * 1024)
    {
        return Status::Limit;
    }
    Fd directory;
    Text<1024> leaf;
    auto status = Parent(root, path, directory, leaf);
    if (status != Status::Ok)
    {
        return status;
    }
    // One cooperative directory lock serializes Ludus writers. External edits
    // are checked again immediately before rename; arbitrary external writers
    // must participate in locking for a strict multi-process compare-and-swap.
    if (flock(directory.Value, LOCK_EX | LOCK_NB) != 0)
    {
        return Status::Conflict;
    }
    auto matches = [&]() noexcept {
        Bytes current;
        const auto readStatus = ReadAt(directory.Value, leaf.Data, usize{32} * 1024 * 1024, current);
        if (expected == nullptr)
        {
            return readStatus == Status::NotFound;
        }
        return readStatus == Status::Ok && Hash(current.Data()) == *expected;
    };
    if (!matches())
    {
        return Status::Conflict;
    }
    Text<1100> temp;
    (void)temp.Set(leaf.View());
    // Exclusive creation prevents overwriting another operation's temporary.
    const auto suffix = std::string_view(".ludus-save");
    for (char ch : suffix)
    {
        temp.Data[temp.Length++] = ch;
    }
    temp.Data[temp.Length] = '\0';
    Fd file;
    file.Value = openat(directory.Value, temp.Data, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file.Value < 0)
    {
        return Status::IoError;
    }
    usize offset = 0;
    status = Status::Ok;
    while (offset < data.size())
    {
        const auto count = write(file.Value, data.data() + offset, data.size() - offset);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            status = Status::IoError;
            break;
        }
        offset += static_cast<usize>(count);
    }
    if (status == Status::Ok && fsync(file.Value) != 0)
    {
        status = Status::IoError;
    }
    if (status == Status::Ok && !matches())
    {
        status = Status::Conflict;
    }
    if (status == Status::Ok && syscall(SYS_renameat, directory.Value, temp.Data, directory.Value, leaf.Data) != 0)
    {
        status = Status::IoError;
    }
    if (status != Status::Ok)
    {
        unlinkat(directory.Value, temp.Data, 0);
    }
    else
    {
        (void)fsync(directory.Value);
    }
    return status;
#else
    (void)root;
    (void)path;
    (void)data;
    (void)expected;
    return Status::Unsupported;
#endif
}
struct FileReader::Impl final
{
    foundation::filesystem::File File;
    uint64 Position = 0;
};
FileReader::~FileReader() noexcept
{
    delete mImpl;
}
Status FileReader::Open(std::string_view root, std::string_view path) noexcept
{
    if (root.empty() || !ValidPath(path))
    {
        return Status::Invalid;
    }
    foundation::filesystem::Directory directory;
    auto result = directory.Open(root);
    if (!result.Succeeded())
    {
        return ContentStatus(result.Code);
    }
    auto* next = new (std::nothrow) Impl();
    if (next == nullptr)
    {
        return Status::OutOfMemory;
    }
    result = directory.OpenRead(path, next->File);
    if (!result.Succeeded())
    {
        delete next;
        return ContentStatus(result.Code);
    }
    delete mImpl;
    mImpl = next;
    return Status::Ok;
}
Status FileReader::Clone(FileReader& output) const noexcept
{
    if (mImpl == nullptr)
    {
        return Status::Invalid;
    }
    auto* next = new (std::nothrow) Impl();
    if (next == nullptr)
    {
        return Status::OutOfMemory;
    }
    const auto result = mImpl->File.Clone(next->File);
    if (!result.Succeeded())
    {
        delete next;
        return ContentStatus(result.Code);
    }
    delete output.mImpl;
    output.mImpl = next;
    return Status::Ok;
}
Status FileReader::Read(std::span<uint8> bytes, usize& count) noexcept
{
    count = 0;
    if (mImpl == nullptr)
    {
        return Status::Invalid;
    }
    const auto read = mImpl->File.ReadAt(mImpl->Position, bytes);
    if (!read.Outcome.Succeeded())
    {
        return ContentStatus(read.Outcome.Code);
    }
    count = read.BytesRead;
    mImpl->Position += count;
    return Status::Ok;
}
Status FileReader::Seek(int64 offset, bool relative) noexcept
{
    if (mImpl == nullptr)
    {
        return Status::Invalid;
    }
    const auto base = relative ? mImpl->Position : 0;
    if (offset < 0)
    {
        const auto magnitude = static_cast<uint64>(-(offset + 1)) + 1;
        if (magnitude > base)
        {
            return Status::Invalid;
        }
        mImpl->Position = base - magnitude;
    }
    else
    {
        const auto forward = static_cast<uint64>(offset);
        if (forward > Size() - base)
        {
            return Status::Invalid;
        }
        mImpl->Position = base + forward;
    }
    return Status::Ok;
}
uint64 FileReader::Size() const noexcept
{
    return mImpl != nullptr ? mImpl->File.Size() : 0;
}

} // namespace ludus::content
