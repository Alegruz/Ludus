#include <ludus/foundation/base/types.h>
#include <ludus/foundation/filesystem/persistence.hpp>

#include "internal/persistence_test_hooks.hpp"

#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(LUDUS_PLATFORM_LINUX)
#    include <sys/syscall.h>
#else
#    include <sys/stdio.h>
#endif

// Test-only driver: exercise the real exception-free Publish implementation.
// Every successful persistence boundary pauses for an external controller.
// The controller can kill this process or mark a dm-log-writes recording without
// introducing another fsync. No hooks or protocol enter the installed SDK.
using namespace ludus::foundation;
using namespace ludus::foundation::filesystem;
namespace
{
int gTemporary = -1;
uint32 gWrites = 0;
constexpr usize WRITE_CHUNK_BYTES = 4096;
constexpr usize MAX_PAYLOAD_BYTES = WRITE_CHUNK_BYTES * 3 + 17;
void ReachBoundary(const char* name) noexcept
{
    std::puts(name);
    std::fflush(stdout);
    if (std::getchar() != '\n')
    {
        ::_exit(90);
    }
}
} // namespace
namespace ludus::foundation::filesystem::persistence_test
{
bool AllocationAllowed() noexcept
{
    return true;
}
int OpenAt(int parent, const char* name, int flags, mode_t mode) noexcept
{
    const int descriptor = ::openat(parent, name, flags, mode);
    if (descriptor >= 0 && (flags & O_CREAT) != 0)
    {
        gTemporary = descriptor;
        ReachBoundary("temporary-created");
    }
    return descriptor;
}
int MetadataAt(int parent, const char* name, struct stat* outInfo, int flags) noexcept
{
    return ::fstatat(parent, name, outInfo, flags);
}
int Lock(int descriptor, int operation) noexcept
{
    return ::flock(descriptor, operation);
}
isize Write(int descriptor, std::span<const uint8> bytes) noexcept
{
    const usize count = bytes.size() < WRITE_CHUNK_BYTES ? bytes.size() : WRITE_CHUNK_BYTES;
    const isize written = ::write(descriptor, bytes.data(), count);
    if (written > 0)
    {
        ++gWrites;
        // Partial positive writes are permitted by Publish's production loop.
        std::printf("write-%u\n", gWrites);
        std::fflush(stdout);
        if (std::getchar() != '\n')
        {
            ::_exit(90);
        }
    }
    return written;
}
int Sync(int descriptor) noexcept
{
    const int result = ::fsync(descriptor);
    if (result == 0)
    {
        ReachBoundary(descriptor == gTemporary ? "file-synced" : "directory-synced");
    }
    return result;
}
int Replace(int parent, const char* source, const char* target) noexcept
{
#if defined(LUDUS_PLATFORM_LINUX)
    const int result = static_cast<int>(::syscall(SYS_renameat, parent, source, parent, target));
#else
    const int result = ::renameat(parent, source, parent, target);
#endif
    if (result == 0)
    {
        ReachBoundary("replaced");
    }
    return result;
}
int Unlink(int parent, const char* name) noexcept
{
    return ::unlinkat(parent, name, 0);
}
int Close(int descriptor) noexcept
{
    const int result = ::close(descriptor);
    if (descriptor == gTemporary)
    {
        gTemporary = -1;
        if (result == 0)
        {
            ReachBoundary("temporary-closed");
        }
    }
    return result;
}
} // namespace ludus::foundation::filesystem::persistence_test
int main(int argumentCount, char** arguments)
{
    // Sizes and policies are deliberately closed vocabulary, with no unchecked
    // numeric parsing or caller-controlled destination pathname.
    if (argumentCount != 4)
    {
        return 1;
    }
    const std::string_view policy(arguments[2]);
    const std::string_view shape(arguments[3]);
    WriteOptions options;
    if (policy == "publish")
    {
        options.Sync = SyncPolicy::PublishOnly;
    }
    else if (policy == "file")
    {
        options.Sync = SyncPolicy::File;
    }
    else if (policy == "directory")
    {
        options.Sync = SyncPolicy::FileAndDirectory;
    }
    else
    {
        return 2;
    }
    const usize size = shape == "empty" ? 0 : MAX_PAYLOAD_BYTES;
    if (shape != "empty" && shape != "multi")
    {
        return 3;
    }
    std::vector<uint8> bytes(size);
    for (usize index = 0; index < size; ++index)
    {
        bytes[index] = static_cast<uint8>((index * 37 + index / 4096) % 251);
    }
    WriteDirectory writer;
    if (writer.Open(arguments[1]).Succeeded() == false)
    {
        return 4;
    }
    const PublicationResult result = writer.Publish("nested/data", bytes, options);
    if (result.Outcome.Succeeded() == false || result.Cleanup.Succeeded() == false || result.Published == false ||
        result.FileSynced != (options.Sync != SyncPolicy::PublishOnly) ||
        result.DirectorySynced != (options.Sync == SyncPolicy::FileAndDirectory))
    {
        return 5;
    }
    ReachBoundary("complete");
    return 0;
}
