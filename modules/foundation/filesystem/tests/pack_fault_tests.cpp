#include <ludus/foundation/filesystem/pack.hpp>

#include "internal/pack_test_hooks.hpp"
#include "pack_fixture.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <span>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
using namespace ludus::foundation::filesystem;
namespace
{
usize gAllocations = 0;
usize gFailAt = ~usize{0};
struct Injection final
{
    explicit Injection(usize index) noexcept
    {
        gAllocations = 0;
        gFailAt = index;
    }
    ~Injection() noexcept
    {
        gFailAt = ~usize{0};
    }
};
struct FaultState final
{
    usize Reads = 0;
    usize FailRead = ~usize{0};
    usize FilesDestroyed = 0;
    Result Outcome{Status::IoError, 73};
    bool Short = false;
};
class ArchiveFile final : public ProviderFile
{
public:
    explicit ArchiveFile(FaultState& state) noexcept : mState(state) {}
    ~ArchiveFile() noexcept override
    {
        ++mState.FilesDestroyed;
    }
    [[nodiscard]] uint64 Size() const noexcept override
    {
        return sizeof(pack_fixture::kPack);
    }
    [[nodiscard]] ReadResult ReadAt(uint64 offset, std::span<uint8> destination) const noexcept override
    {
        if (offset > Size())
        {
            return {{Status::InvalidArgument}};
        }
        const bool fail = mState.Reads++ == mState.FailRead;
        if (fail)
        {
            return mState.Short ? ReadResult{} : ReadResult{mState.Outcome};
        }
        const usize count = std::min(destination.size(), static_cast<usize>(Size() - offset));
        if (count != 0)
        {
            std::memcpy(destination.data(), pack_fixture::kPack + static_cast<usize>(offset), count);
        }
        return {{}, count};
    }

private:
    FaultState& mState;
};
class ArchiveProvider final : public Provider
{
public:
    explicit ArchiveProvider(FaultState& state) noexcept : mState(state) {}
    [[nodiscard]] Result OpenRead(std::string_view, ProviderFile*& output) const noexcept override
    {
        auto* next = new (std::nothrow) ArchiveFile(mState);
        if (next == nullptr)
        {
            return {Status::OutOfMemory};
        }
        output = next;
        return {};
    }

private:
    FaultState& mState;
};
ProviderHandle Create(const ProviderHandle& source)
{
    ProviderHandle pack;
    REQUIRE(CreatePackProvider(source, "archive", {}, pack).Succeeded());
    return pack;
}
} // namespace
namespace ludus::foundation::filesystem::test
{
bool PackAllocationAllowed() noexcept
{
    return gAllocations++ != gFailAt;
}
} // namespace ludus::foundation::filesystem::test

TEST_CASE("Each pack metadata and open allocation failure preserves output and releases staged ownership")
{
    FaultState state;
    const ProviderHandle source(new ArchiveProvider(state));
    const auto before = gAllocations;
    auto pack = Create(source);
    const auto allocations = gAllocations - before;
    REQUIRE(allocations == 5);
    const auto* previous = pack.Get();
    for (usize failure = 0; failure < allocations; ++failure)
    {
        const auto destroyed = state.FilesDestroyed;
        Result result;
        {
            const Injection fault(failure);
            result = CreatePackProvider(source, "archive", {}, pack);
        }
        REQUIRE(result.Code == Status::OutOfMemory);
        REQUIRE(pack.Get() == previous);
        REQUIRE(state.FilesDestroyed == destroyed + (failure == 0 ? 0 : 1));
    }
    ProviderFile* file = nullptr;
    REQUIRE(pack.Get()->OpenRead("compressed", file).Succeeded());
    auto* priorFile = file;
    for (usize failure = 0; failure < 2; ++failure)
    {
        Result result;
        {
            const Injection fault(failure);
            result = pack.Get()->OpenRead("compressed", file);
        }
        REQUIRE(result.Code == Status::OutOfMemory);
        REQUIRE(file == priorFile);
    }
    uint8 bytes[8]{};
    const auto calls = gAllocations;
    REQUIRE(file->ReadAt(65530, bytes).BytesRead == 8);
    REQUIRE(file->ReadAt(file->Size(), {}).Outcome.Succeeded());
    REQUIRE(gAllocations == calls);
    delete file;
}
TEST_CASE("Pack mount preserves native faults and treats successful short metadata reads as corruption")
{
    for (usize failure = 0; failure < 3; ++failure)
    {
        FaultState state;
        const ProviderHandle source(new ArchiveProvider(state));
        auto pack = Create(source);
        const auto* previous = pack.Get();
        state.Reads = 0;
        state.FailRead = failure; // Header, index, or final revision check.
        REQUIRE(CreatePackProvider(source, "archive", {}, pack).Code == Status::IoError);
        state.Reads = 0;
        REQUIRE(CreatePackProvider(source, "archive", {}, pack).NativeCode == 73);
        REQUIRE(pack.Get() == previous);
        REQUIRE(state.FilesDestroyed == 2);
    }
    FaultState state;
    const ProviderHandle source(new ArchiveProvider(state));
    auto pack = Create(source);
    state.Reads = 0;
    state.FailRead = 1;
    state.Short = true;
    REQUIRE(CreatePackProvider(source, "archive", {}, pack).Code == Status::CorruptData);
}
TEST_CASE("Read and EOF revision faults preserve only verified bytes and Changed invalidates all progress")
{
    for (const auto code : {Status::IoError, Status::Changed})
    {
        FaultState state;
        const ProviderHandle source(new ArchiveProvider(state));
        auto pack = Create(source);
        ProviderFile* file = nullptr;
        REQUIRE(pack.Get()->OpenRead("compressed", file).Succeeded());
        uint8 bytes[32]{};
        state.Reads = 0;
        state.FailRead = 2; // Initial check and first verified block precede the failed second block.
        state.Outcome = {code, 73};
        const auto read = file->ReadAt(65530, bytes);
        REQUIRE(read.Outcome.Code == code);
        REQUIRE(read.Outcome.NativeCode == 73);
        REQUIRE(read.BytesRead == (code == Status::Changed ? 0 : 6));
        state.Reads = 0;
        state.FailRead = 0;
        REQUIRE(file->ReadAt(file->Size(), {}).Outcome.Code == code);
        ProviderFile* previous = file;
        state.Reads = 0;
        REQUIRE(pack.Get()->OpenRead("compressed", file).Code == code);
        REQUIRE(file == previous);
        state.Reads = 0;
        REQUIRE(pack.Get()->OpenRead("missing", file).Code == code);
        delete file;
    }
}

TEST_CASE("Directly opened pack files own their archive and index after all provider handles are released")
{
    FaultState state;
    ProviderHandle source(new ArchiveProvider(state));
    ProviderFile* file = nullptr;
    {
        const auto pack = Create(source);
        REQUIRE(pack.Get()->OpenRead("compressed", file).Succeeded());
    }
    source = {};
    REQUIRE(state.FilesDestroyed == 0);
    uint8 bytes[8]{};
    REQUIRE(file->ReadAt(70000, bytes).Outcome.Succeeded());
    REQUIRE(bytes[0] == 't');
    REQUIRE(file->Size() == 70013);
    delete file;
    REQUIRE(state.FilesDestroyed == 1);
}
