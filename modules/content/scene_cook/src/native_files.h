#pragma once

// Private POSIX helpers for the scene pipeline: path resolution, directory
// creation and atomic whole-file replacement of arbitrary size.

#include <ludus/foundation/base/config.h>

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/strings/string.hpp>

#include <ludus/content/content.h>
#include <ludus/content/scene_cook/scene_cook.h>

#include <span>
#include <string_view>

namespace ludus::content::scene_cook::detail
{
/// realpath(3) of an existing directory.
[[nodiscard]] Status ResolveDirectory(std::string_view path, foundation::String& outPath) noexcept;
/// Final component of a resolved path.
[[nodiscard]] std::string_view GetBaseName(std::string_view path) noexcept;
/// mkdir -p with mode 0777 (the process umask applies).
[[nodiscard]] Status MakeDirectories(std::string_view path) noexcept;
/// Replace directory/name with data via a same-directory temporary and rename.
[[nodiscard]] Status
WriteFileAtomic(std::string_view directory, std::string_view name, std::span<const uint8> data) noexcept;

/// os.path.realpath(path, strict=False): symlinks in existing components are
/// followed, missing components are appended lexically; relative paths are
/// taken from the current directory.
[[nodiscard]] Status ResolvePath(std::string_view path, foundation::String& outPath) noexcept;
/// Path.exists(): stat(2) succeeds, following symlinks.
[[nodiscard]] bool DoesPathExist(std::string_view path) noexcept;
/// scene_cook.bounded_bytes: the whole file, Limit when it exceeds maximum bytes.
[[nodiscard]] Status ReadFileBounded(std::string_view path, usize maximum, foundation::Array<uint8>& outData) noexcept;
/// SHA-256 of a whole file, streamed in bounded memory (no size limit).
/// @return Ok, NotFound, IoError or Unsupported; outDigest is set only on Ok.
[[nodiscard]] Status HashFile(std::string_view path, content::Digest& outDigest) noexcept;

/// Sequential read-only file handle (POSIX open/read), closed on destruction.
class SequentialFile final
{
public:
    SequentialFile() noexcept = default;
    ~SequentialFile() noexcept
    {
        Close();
    }
    SequentialFile(const SequentialFile&) = delete;
    SequentialFile& operator=(const SequentialFile&) = delete;
    /// Open path for reading. @return Ok, NotFound or IoError.
    [[nodiscard]] Status Open(std::string_view path) noexcept;
    /// Read up to outBuffer.size() bytes; outCount is zero at end of file.
    [[nodiscard]] Status Read(std::span<uint8> outBuffer, usize& outCount) noexcept;
    /// Close the file if open; Open may be called again afterwards.
    void Close() noexcept;

private:
    /// POSIX descriptor; unused where SequentialFile is unsupported.
    [[maybe_unused]] int mDescriptor = -1;
};

/// How PublishFile commits its synced temporary.
enum class PublishMode : uint8
{
    /// os.replace over any existing file.
    Replace,
    /// os.link; an existing file must already hold identical bytes.
    Immutable,
};
/// Outcome of PublishFile beyond plain I/O failure.
enum class PublishResult : uint8
{
    Ok,
    /// The cancellation hook fired after the data was synced.
    Cancelled,
    /// An immutable destination exists with different bytes.
    Conflict,
    /// An immutable destination exists but could not be read back.
    ReadFailure,
    IoError,
};
/// scene_assets.atomic: mkstemp(".pending-") beside path (mode 0600), write,
/// fsync, commit, fsync the directory; the temporary is always removed.
[[nodiscard]] PublishResult PublishFile(std::string_view path,
                                        std::span<const uint8> data,
                                        PublishMode mode,
                                        const Cancellation* cancelOrNull) noexcept;
/// rename(2), used for os.replace and os.rename.
[[nodiscard]] Status RenamePath(std::string_view from, std::string_view to) noexcept;
/// unlink(2).
[[nodiscard]] Status RemoveFile(std::string_view path) noexcept;
/// mkdir(2) of one directory with mode 0777 (the umask applies).
[[nodiscard]] Status MakeDirectory(std::string_view path) noexcept;
/// fsync(2) of a directory.
[[nodiscard]] Status SyncDirectory(std::string_view path) noexcept;
/// tempfile.mkdtemp(prefix, dir=parent): a fresh mode 0700 directory.
[[nodiscard]] Status
MakeTemporaryDirectory(std::string_view parent, std::string_view prefix, foundation::String& outPath) noexcept;
/// shutil.rmtree without following symlinks.
[[nodiscard]] Status RemoveTree(std::string_view path) noexcept;

/// flock(2) mode.
enum class LockMode : uint8
{
    Shared,
    Exclusive,
};
/// Non-blocking advisory whole-file lock, released on destruction.
class FileLock final
{
public:
    FileLock() noexcept = default;
#if LUDUS_TARGET_OS == LUDUS_OS_LINUX || LUDUS_TARGET_OS == LUDUS_OS_MACOS
    ~FileLock() noexcept;
#else
    ~FileLock() noexcept = default;
#endif
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    /// Open path (created in append mode when exclusive) and lock it.
    /// @return Ok, Conflict when another holder blocks the lock, IoError otherwise.
    [[nodiscard]] Status Acquire(std::string_view path, LockMode mode) noexcept;

private:
#if LUDUS_TARGET_OS == LUDUS_OS_LINUX || LUDUS_TARGET_OS == LUDUS_OS_MACOS
    int mDescriptor = -1;
#endif
};
} // namespace ludus::content::scene_cook::detail
