#include <ludus/foundation/filesystem/filesystem.hpp>

#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>

#include <fcntl.h>
#include <unistd.h>

using namespace ludus::foundation;
using namespace ludus::foundation::filesystem;
namespace
{
enum class Fault : uint8
{
    None,
    Interrupt,
    Short,
    PartialError,
    Mutate,
};
Fault gFault = Fault::None;
usize gCalls = 0;
int gWriter = -1;
} // namespace
// GNU --wrap requires these exact reserved symbol names. This test-only
// interposer follows the existing FoundationBase allocation-test convention.
// NOLINTBEGIN(bugprone-reserved-identifier)
extern "C" isize __real_pread64(int descriptor, void* buffer, usize count, off_t offset);
extern "C" isize __wrap_pread64(int descriptor, void* buffer, usize count, off_t offset)
{
    if (gFault == Fault::None)
    {
        return __real_pread64(descriptor, buffer, count, offset);
    }
    ++gCalls;
    if (gFault == Fault::Interrupt && gCalls == 1)
    {
        errno = EINTR;
        return -1;
    }
    if (gFault == Fault::PartialError && gCalls == 2)
    {
        errno = EIO;
        return -1;
    }
    const usize actual = (gFault == Fault::Short || gFault == Fault::PartialError) && count > 2 ? 2 : count;
    const auto received = __real_pread64(descriptor, buffer, actual, offset);
    if (gFault == Fault::Mutate && ::ftruncate(gWriter, 1) != 0)
    {
        return -1;
    }
    return received;
}
// NOLINTEND(bugprone-reserved-identifier)
TEST_CASE("Offset reads retry interrupts fill short transfers and preserve failure progress")
{
    char path[] = "/tmp/ludus-fs-fault-XXXXXX";
    REQUIRE(::mkdtemp(path) != nullptr);
    struct Cleanup final
    {
        const char* Path;
        ~Cleanup()
        {
            gFault = Fault::None;
            if (gWriter >= 0)
            {
                ::close(gWriter);
                gWriter = -1;
            }
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }
    } cleanup{path};
    const auto filename = std::filesystem::path(path) / "data";
    {
        std::ofstream file(filename, std::ios::binary);
        file << "abcdef";
        REQUIRE(file.good());
    }
    Directory root;
    File file;
    REQUIRE(root.Open(path).Succeeded());
    REQUIRE(root.OpenRead("data", file).Succeeded());
    uint8 bytes[6]{};
    for (const auto fault : {Fault::Interrupt, Fault::Short})
    {
        gFault = fault;
        gCalls = 0;
        const auto read = file.ReadAt(0, bytes);
        gFault = Fault::None;
        REQUIRE(read.Outcome.Succeeded());
        REQUIRE(read.BytesRead == 6);
        REQUIRE(gCalls > 1);
        REQUIRE(bytes[0] == 'a');
        REQUIRE(bytes[5] == 'f');
    }
    gFault = Fault::PartialError;
    gCalls = 0;
    auto read = file.ReadAt(0, bytes);
    gFault = Fault::None;
    REQUIRE(read.Outcome.Code == Status::IoError);
    REQUIRE(read.Outcome.NativeCode == EIO);
    REQUIRE(read.BytesRead == 2);
    gWriter = ::open(filename.c_str(), O_WRONLY | O_CLOEXEC);
    REQUIRE(gWriter >= 0);
    gFault = Fault::Mutate;
    gCalls = 0;
    read = file.ReadAt(0, bytes);
    gFault = Fault::None;
    REQUIRE(read.Outcome.Code == Status::Changed);
    REQUIRE(read.BytesRead == 0);
}

namespace
{
bool gTrackAllocation = false;
bool gFailAllocation = false;
usize gAllocations = 0;
} // namespace
// GNU --wrap and the Itanium ABI require these exact names for nothrow new.
// NOLINTBEGIN(bugprone-reserved-identifier)
extern "C" void* __real__ZnwmRKSt9nothrow_t(usize size, const std::nothrow_t& tag) noexcept;
extern "C" void* __wrap__ZnwmRKSt9nothrow_t(usize size, const std::nothrow_t& tag) noexcept
{
    if (gTrackAllocation)
    {
        ++gAllocations;
    }
    return gFailAllocation ? nullptr : __real__ZnwmRKSt9nothrow_t(size, tag);
}
// NOLINTEND(bugprone-reserved-identifier)
TEST_CASE("Open and clone allocation failures preserve owners and release staged descriptors")
{
    Directory root;
    File file, clone;
    REQUIRE(root.Open("/proc/self").Succeeded());
    REQUIRE(root.OpenRead("status", file).Succeeded());
    REQUIRE(file.Clone(clone).Succeeded());
    const auto countDescriptors = [] {
        usize count = 0;
        for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator("/proc/self/fd"))
        {
            ++count;
        }
        return count;
    };
    const auto before = countDescriptors();
    gFailAllocation = true;
    const auto reopen = root.Open("/tmp");
    const auto open = root.OpenRead("status", file);
    const auto duplicate = file.Clone(clone);
    gFailAllocation = false;
    REQUIRE(reopen.Code == Status::OutOfMemory);
    REQUIRE(open.Code == Status::OutOfMemory);
    REQUIRE(duplicate.Code == Status::OutOfMemory);
    REQUIRE(root.IsOpen());
    REQUIRE(file.IsOpen());
    REQUIRE(clone.IsOpen());
    REQUIRE(countDescriptors() == before);
}
TEST_CASE("Warm reads and path validation do not allocate")
{
    Directory root;
    File file;
    REQUIRE(root.Open("/tmp").Succeeded());
    char name[] = "/tmp/ludus-fs-allocation-XXXXXX";
    const int descriptor = ::mkstemp(name);
    REQUIRE(descriptor >= 0);
    REQUIRE(::write(descriptor, "abcdef", 6) == 6);
    REQUIRE(::close(descriptor) == 0);
    REQUIRE(root.OpenRead(std::string_view(name).substr(5), file).Succeeded());
    REQUIRE(::unlink(name) == 0);
    uint8 bytes[6]{};
    gAllocations = 0;
    gTrackAllocation = true;
    const bool pathValid = ValidPath("nested/音.wav");
    const auto read = file.ReadAt(0, bytes);
    gTrackAllocation = false;
    REQUIRE(pathValid);
    REQUIRE(read.Outcome.Succeeded());
    REQUIRE(read.BytesRead == 6);
    REQUIRE(gAllocations == 0);
}
