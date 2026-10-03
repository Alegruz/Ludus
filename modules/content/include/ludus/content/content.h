#pragma once

#include <ludus/foundation/base/core.h>

#include <span>
#include <string_view>

namespace ludus::content
{
using namespace ludus::foundation;

inline constexpr usize MAX_DOCUMENT_BYTES = usize{1024} * 1024;
inline constexpr usize MAX_RESOURCES = 4096;

enum class Status : uint8
{
    Ok,
    Pending,
    Invalid,
    Limit,
    OutOfMemory,
    IoError,
    Conflict,
    NotFound,
    Unsupported,
    Cancelled,
};

struct Diagnostic final
{
    Status Result = Status::Ok;
    char Field[128]{};
    usize Offset = 0;
};

template <usize N>
struct Text final
{
    char Data[N + 1]{};
    usize Length = 0;
    [[nodiscard]] bool Set(std::string_view value) noexcept
    {
        if (value.size() > N)
        {
            return false;
        }
        for (usize i = 0; i < value.size(); ++i)
        {
            Data[i] = value[i];
        }
        Length = value.size();
        Data[Length] = '\0';
        return true;
    }
    [[nodiscard]] std::string_view View() const noexcept
    {
        return Length <= N ? std::string_view(Data, Length) : std::string_view{};
    }
};
using ResourceId = Text<128>;
using ResourcePath = Text<1024>;

class Bytes final
{
public:
    Bytes() noexcept = default;
    ~Bytes() noexcept;
    Bytes(const Bytes&) = delete;
    Bytes& operator=(const Bytes&) = delete;
    Bytes(Bytes&& other) noexcept;
    Bytes& operator=(Bytes&& other) noexcept;
    [[nodiscard]] bool Resize(usize size) noexcept;
    [[nodiscard]] std::span<uint8> Data() noexcept
    {
        return {mData, mSize};
    }
    [[nodiscard]] std::span<const uint8> Data() const noexcept
    {
        return {mData, mSize};
    }
    [[nodiscard]] std::string_view String() const noexcept
    {
        return {reinterpret_cast<const char*>(mData), mSize};
    }

private:
    uint8* mData = nullptr;
    usize mSize = 0;
};

struct Digest final
{
    uint8 Data[32]{};
    [[nodiscard]] bool operator==(const Digest&) const noexcept = default;
};
class Hasher final
{
public:
    [[nodiscard]] bool Add(std::span<const uint8> data) noexcept;
    [[nodiscard]] Digest Finish() const noexcept;

private:
    uint32 mState[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8 mTail[64]{};
    usize mCount = 0;
    uint64 mBytes = 0;
};
[[nodiscard]] Digest Hash(std::span<const uint8> data) noexcept;
[[nodiscard]] bool ValidId(std::string_view value) noexcept;
[[nodiscard]] bool ValidPath(std::string_view value) noexcept;

// Native file operations never throw. Roots and paths are validated independently;
// reads are capped, symlink escapes rejected, saves compare the expected digest.
[[nodiscard]] Status ReadFile(std::string_view root, std::string_view path, usize cap, Bytes& output) noexcept;
[[nodiscard]] Status
SaveFile(std::string_view root, std::string_view path, std::span<const uint8> data, const Digest* expected) noexcept;

// A descriptor-bound regular file. Atomic replacement leaves an open revision
// intact; in-place size/timestamp changes fail subsequent reads. Clone creates
// independent seek state, never an unvalidated pathname reopen.
class FileReader final
{
public:
    FileReader() noexcept = default;
    ~FileReader() noexcept;
    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;
    [[nodiscard]] Status Open(std::string_view root, std::string_view path) noexcept;
    [[nodiscard]] Status Clone(FileReader& output) const noexcept;
    [[nodiscard]] Status Read(std::span<uint8> bytes, usize& count) noexcept;
    [[nodiscard]] Status Seek(int64 offset, bool relative) noexcept;
    [[nodiscard]] uint64 Size() const noexcept;

private:
    struct Impl;
    Impl* mImpl = nullptr;
};
// Caller owns the returned catalog; loading is transactional.
enum class Kind : uint8
{
    AudioSource,
    Sound,
    Music
};
struct Resource final
{
    ResourceId Id;
    ResourcePath Path;
    Kind Type = Kind::AudioSource;
};
class Catalog final
{
public:
    Catalog() noexcept = default;
    ~Catalog() noexcept;
    Catalog(Catalog&& other) noexcept;
    Catalog& operator=(Catalog&& other) noexcept;
    Catalog(const Catalog&) = delete;
    Catalog& operator=(const Catalog&) = delete;
    [[nodiscard]] Status Read(std::string_view json, Diagnostic& diagnostic) noexcept;
    [[nodiscard]] Status Write(Bytes& output) const noexcept;
    [[nodiscard]] Status Put(const Resource& resource) noexcept;
    [[nodiscard]] const Resource* Find(std::string_view id) const noexcept;
    [[nodiscard]] std::span<const Resource> Entries() const noexcept
    {
        return {mEntries, mCount};
    }

private:
    Resource* mEntries = nullptr;
    usize mCount = 0;
};
} // namespace ludus::content
