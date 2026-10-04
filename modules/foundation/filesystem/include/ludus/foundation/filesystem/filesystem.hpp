#pragma once

#include <ludus/foundation/base/core.h>

#include <span>
#include <string_view>

namespace ludus::foundation::filesystem
{
inline constexpr usize MAX_PATH_BYTES = 1024;
inline constexpr usize MAX_ROOT_BYTES = 4096;

enum class Status : uint8
{
    Ok,
    InvalidArgument,
    NotFound,
    AccessDenied,
    NotRegularFile,
    OutOfMemory,
    IoError,
    Changed,
    Unsupported,
};

struct Result final
{
    Status Code = Status::Ok;
    int32 NativeCode = 0;
    [[nodiscard]] bool Succeeded() const noexcept
    {
        return Code == Status::Ok;
    }
};

struct ReadResult final
{
    Result Outcome;
    usize BytesRead = 0;
};

// Byte-exact UTF-8 relative path; no normalization or host case folding.
[[nodiscard]] bool ValidPath(std::string_view path) noexcept;

// Thanks to Noel Llopis and Charles Nicholson, "Stay in the Game: Asset
// Hotloading for Fast Iteration", Game Programming Gems 6, sec. 1.10,
// pp. 109-114: their prepare/rebind boundary informs revision ownership here.
// Active file readers retain old bytes; Content owns asset rebinding. Design
// inspiration only, no article code copied. See docs/architecture/filesystem.md.
// Owns an opened regular-file revision. Concurrent const operations are safe
// with disjoint destination buffers. Lifetime changes require exclusive access.
class File final
{
public:
    File() noexcept = default;
    ~File() noexcept;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;
    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] uint64 Size() const noexcept;
    // Transactional, duplicates the opened revision without pathname lookup.
    [[nodiscard]] Result Clone(File& output) const noexcept;
    // Fills to captured EOF; offsets > Size() fail. EINTR and short reads handled.
    // On Changed, BytesRead=0 and the touched buffer must be discarded. Other
    // failures report partial progress. Metadata detection is best-effort only.
    [[nodiscard]] ReadResult ReadAt(uint64 offset, std::span<uint8> destination) const noexcept;
    // Releases ownership even on error. Destruction closes best-effort.
    [[nodiscard]] Result Close() noexcept;

private:
    friend class Directory;
    struct Impl;
    Impl* mImpl = nullptr;
};

// Host-selected trusted root, pinned once. Child symlinks are rejected at every
// component. Not a sandbox against hostile renames, hard links or mount changes.
class Directory final
{
public:
    Directory() noexcept = default;
    ~Directory() noexcept;
    Directory(const Directory&) = delete;
    Directory& operator=(const Directory&) = delete;
    Directory(Directory&& other) noexcept;
    Directory& operator=(Directory&& other) noexcept;
    [[nodiscard]] bool IsOpen() const noexcept;
    // Failure preserves the existing root/output. Roots may have symlink ancestry.
    [[nodiscard]] Result Open(std::string_view nativeRoot) noexcept;
    [[nodiscard]] Result OpenRead(std::string_view relativePath, File& output) const noexcept;
    [[nodiscard]] Result Close() noexcept;

private:
    struct Impl;
    Impl* mImpl = nullptr;
};
} // namespace ludus::foundation::filesystem
