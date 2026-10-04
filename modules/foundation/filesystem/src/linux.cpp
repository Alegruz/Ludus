#include <ludus/foundation/base/checked_integer.hpp>
#include <ludus/foundation/filesystem/filesystem.hpp>

// Thanks to the Linux man-pages project, open(2), pread(2) and close(2),
// https://man7.org/linux/man-pages/man2/open.2.html : descriptor
// revision lifetime, independent offsets, short reads and Linux close ownership
// inform this backend. The component walk adds ancestor symlink rejection; it
// does not claim openat2 sandbox guarantees. Independently implemented; see
// docs/architecture/filesystem.md, "Reference review and design revision".

#include <new>

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ludus::foundation::filesystem
{
namespace
{
static_assert(sizeof(off_t) >= sizeof(int64));

Result NativeFailure(int error) noexcept
{
    Status code = Status::IoError;
    if (error == ENOENT)
    {
        code = Status::NotFound;
    }
    else if (error == EACCES || error == EPERM || error == ELOOP)
    {
        code = Status::AccessDenied;
    }
    else if (error == ENOTDIR || error == ENAMETOOLONG)
    {
        code = Status::InvalidArgument;
    }
    return {code, static_cast<int32>(error)};
}
struct Descriptor final
{
    int Value = -1;
    Descriptor() noexcept = default;
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    ~Descriptor() noexcept
    {
        (void)Close();
    }
    [[nodiscard]] Result Close() noexcept
    {
        const int value = Value;
        Value = -1;
        // Linux releases ownership even on EINTR; retry could close a reused fd.
        return value >= 0 && ::close(value) != 0 ? NativeFailure(errno) : Result{};
    }
};
int OpenAt(int parent, const char* name, int flags) noexcept
{
    int value;
    do
    {
        value = ::openat(parent, name, flags);
    } while (value < 0 && errno == EINTR);
    return value;
}
int Duplicate(int descriptor) noexcept
{
    int value;
    do
    {
        value = ::fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
    } while (value < 0 && errno == EINTR);
    return value;
}
Result Metadata(int descriptor, struct stat& info) noexcept
{
    int value;
    do
    {
        value = ::fstat(descriptor, &info);
    } while (value < 0 && errno == EINTR);
    return value == 0 ? Result{} : NativeFailure(errno);
}
Result CheckRevision(int descriptor, const struct stat& original) noexcept
{
    struct stat current
    {
    };
    const auto result = Metadata(descriptor, current);
    if (!result.Succeeded())
    {
        return result;
    }
    // ctime changes when a path is unlinked/replaced; that must not invalidate
    // an otherwise unchanged opened revision. Size/mtime detection is best-effort.
    if (current.st_size != original.st_size || current.st_mtim.tv_sec != original.st_mtim.tv_sec ||
        current.st_mtim.tv_nsec != original.st_mtim.tv_nsec)
    {
        return {Status::Changed};
    }
    return {};
}
void CopyName(std::string_view text, char* output) noexcept
{
    for (usize i = 0; i < text.size(); ++i)
    {
        output[i] = text[i];
    }
    output[text.size()] = '\0';
}
} // namespace

struct File::Impl final
{
    Descriptor Handle;
    struct stat Original
    {
    };
    uint64 Length = 0;
};
struct Directory::Impl final
{
    Descriptor Handle;
};
File::~File() noexcept
{
    (void)Close();
}
File::File(File&& other) noexcept : mImpl(other.mImpl)
{
    other.mImpl = nullptr;
}
File& File::operator=(File&& other) noexcept
{
    if (this != &other)
    {
        (void)Close();
        mImpl = other.mImpl;
        other.mImpl = nullptr;
    }
    return *this;
}
bool File::IsOpen() const noexcept
{
    return mImpl != nullptr;
}
uint64 File::Size() const noexcept
{
    return mImpl != nullptr ? mImpl->Length : 0;
}
Result File::Close() noexcept
{
    if (mImpl == nullptr)
    {
        return {};
    }
    const auto result = mImpl->Handle.Close();
    delete mImpl;
    mImpl = nullptr;
    return result;
}
Result File::Clone(File& output) const noexcept
{
    if (mImpl == nullptr)
    {
        return {Status::InvalidArgument};
    }
    auto* next = new (std::nothrow) Impl();
    if (next == nullptr)
    {
        return {Status::OutOfMemory};
    }
    next->Handle.Value = Duplicate(mImpl->Handle.Value);
    if (next->Handle.Value < 0)
    {
        const auto result = NativeFailure(errno);
        delete next;
        return result;
    }
    next->Original = mImpl->Original;
    next->Length = mImpl->Length;
    (void)output.Close();
    output.mImpl = next;
    return {};
}
ReadResult File::ReadAt(uint64 offset, std::span<uint8> destination) const noexcept
{
    if (mImpl == nullptr || offset > Size())
    {
        return {{Status::InvalidArgument}};
    }
    auto result = CheckRevision(mImpl->Handle.Value, mImpl->Original);
    if (!result.Succeeded())
    {
        return {result};
    }
    const uint64 left = Size() - offset;
    usize wanted = destination.size();
    if (left < wanted && !TryIntegerCast(left, wanted))
    {
        return {{Status::InvalidArgument}};
    }
    usize total = 0;
    while (total < wanted)
    {
        // Linux limits each read to 0x7ffff000 bytes, independent of bitness.
        constexpr usize MAX_TRANSFER = 0x7ffff000;
        const usize remaining = wanted - total;
        const usize count = remaining < MAX_TRANSFER ? remaining : MAX_TRANSFER;
        off_t nativeOffset = 0;
        // offset + total is bounded by captured Length (a nonnegative off_t).
        if (!TryIntegerCast(offset + total, nativeOffset))
        {
            return {{Status::InvalidArgument}, total};
        }
        isize received;
        do
        {
            received = ::pread(mImpl->Handle.Value, destination.data() + total, count, nativeOffset);
        } while (received < 0 && errno == EINTR);
        if (received < 0)
        {
            return {NativeFailure(errno), total};
        }
        if (received == 0)
        {
            result = CheckRevision(mImpl->Handle.Value, mImpl->Original);
            return {result.Succeeded() ? Result{Status::IoError} : result, result.Code == Status::Changed ? 0 : total};
        }
        total += static_cast<usize>(received);
    }
    result = CheckRevision(mImpl->Handle.Value, mImpl->Original);
    return {result, result.Code == Status::Changed ? 0 : total};
}
Directory::~Directory() noexcept
{
    (void)Close();
}
Directory::Directory(Directory&& other) noexcept : mImpl(other.mImpl)
{
    other.mImpl = nullptr;
}
Directory& Directory::operator=(Directory&& other) noexcept
{
    if (this != &other)
    {
        (void)Close();
        mImpl = other.mImpl;
        other.mImpl = nullptr;
    }
    return *this;
}
bool Directory::IsOpen() const noexcept
{
    return mImpl != nullptr;
}
Result Directory::Close() noexcept
{
    if (mImpl == nullptr)
    {
        return {};
    }
    const auto result = mImpl->Handle.Close();
    delete mImpl;
    mImpl = nullptr;
    return result;
}
Result Directory::Open(std::string_view nativeRoot) noexcept
{
    if (nativeRoot.empty() || nativeRoot.size() > MAX_ROOT_BYTES || nativeRoot.find('\0') != std::string_view::npos)
    {
        return {Status::InvalidArgument};
    }
    char name[MAX_ROOT_BYTES + 1];
    CopyName(nativeRoot, name);
    auto* next = new (std::nothrow) Impl();
    if (next == nullptr)
    {
        return {Status::OutOfMemory};
    }
    next->Handle.Value = OpenAt(AT_FDCWD, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (next->Handle.Value < 0)
    {
        const auto result = NativeFailure(errno);
        delete next;
        return result;
    }
    (void)Close();
    mImpl = next;
    return {};
}
Result Directory::OpenRead(std::string_view relativePath, File& output) const noexcept
{
    if (mImpl == nullptr || !ValidPath(relativePath))
    {
        return {Status::InvalidArgument};
    }
    Descriptor parent;
    int current = mImpl->Handle.Value;
    usize begin = 0;
    char name[MAX_PATH_BYTES + 1];
    while (true)
    {
        const auto slash = relativePath.find('/', begin);
        const auto end = slash == std::string_view::npos ? relativePath.size() : slash;
        CopyName(relativePath.substr(begin, end - begin), name);
        if (slash == std::string_view::npos)
        {
            break;
        }
        const int next = OpenAt(current, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0)
        {
            return NativeFailure(errno);
        }
        (void)parent.Close();
        parent.Value = next;
        current = next;
        begin = slash + 1;
    }
    Descriptor file;
    // NONBLOCK avoids blocking on a FIFO before fstat rejects non-regular files.
    file.Value = OpenAt(current, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
    if (file.Value < 0)
    {
        return NativeFailure(errno);
    }
    struct stat info
    {
    };
    const auto result = Metadata(file.Value, info);
    if (!result.Succeeded())
    {
        return result;
    }
    if (!S_ISREG(info.st_mode) || info.st_size < 0)
    {
        return {Status::NotRegularFile};
    }
    auto* next = new (std::nothrow) File::Impl();
    if (next == nullptr)
    {
        return {Status::OutOfMemory};
    }
    if (!TryIntegerCast(info.st_size, next->Length))
    {
        delete next;
        return {Status::InvalidArgument};
    }
    next->Original = info;
    next->Handle.Value = file.Value;
    file.Value = -1;
    (void)output.Close();
    output.mImpl = next;
    return {};
}
} // namespace ludus::foundation::filesystem
