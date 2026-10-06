#pragma once

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/filesystem/filesystem.hpp>

#include <atomic>
#include <span>
#include <string_view>

namespace ludus::foundation::filesystem
{
/// Host-selected read namespace. Write authorization remains a separate policy.
enum class Root : uint8
{
    Assets,  ///< Published asset bytes.
    Project, ///< Authoring project bytes.
    Cache,   ///< Reconstructible cached bytes.
    User,    ///< User-owned persistent bytes.
};

/// Owned, byte-exact logical key: explicit root plus bounded UTF-8 relative path.
/// No normalization, case folding or allocation; default construction is invalid.
class VirtualPath final
{
public:
    /// Validates and copies the key; failure preserves this value. Uses ValidPath rules.
    [[nodiscard]] Result Set(Root root, std::string_view relativePath) noexcept;
    /// Compares root and exact UTF-8 bytes; two default invalid keys compare equal.
    [[nodiscard]] bool operator==(const VirtualPath& other) const noexcept;
    /// Returns whether a key has been assigned successfully.
    [[nodiscard]] bool IsValid() const noexcept;
    /// Returns the root; meaningful only when IsValid().
    [[nodiscard]] Root GetRoot() const noexcept;
    /// Returns owned path bytes, valid until this object is changed or destroyed.
    [[nodiscard]] std::string_view Path() const noexcept;

private:
    Root mRoot = Root::Assets;
    usize mLength = 0;
    char mBytes[MAX_PATH_BYTES]{};
};

/// Independently owned opened provider revision; destruction releases it best-effort.
/// Const operations must support concurrent reads into disjoint caller-owned buffers.
class ProviderFile
{
public:
    /// Releases the opened revision without logging or errors.
    virtual ~ProviderFile() noexcept = default;
    /// Returns the captured byte length.
    [[nodiscard]] virtual uint64 Size() const noexcept = 0;
    /// Uses File::ReadAt semantics: fills to captured EOF, reports partial errors,
    /// returns zero valid bytes on Changed, and performs no warm-read allocation.
    [[nodiscard]] virtual ReadResult ReadAt(uint64 offset, std::span<uint8> destination) const noexcept = 0;
};

/// Intrusively retained byte provider. Construct on the heap and adopt exactly once.
/// Providers must support concurrent const opens and keep each returned file alive
/// independently of the provider. No exceptions may cross these interfaces.
class Provider
{
public:
    /// Opens a ValidPath key; success transfers a non-null independently owned file.
    /// Failure preserves output. Only NotFound permits a mount lookup to continue.
    [[nodiscard]] virtual Result OpenRead(std::string_view relativePath, ProviderFile*& output) const noexcept = 0;

protected:
    /// Creates the initial reference for a ProviderHandle to adopt.
    Provider() noexcept = default;
    /// Invoked on the final reference; releases resources best-effort.
    virtual ~Provider() noexcept = default;

private:
    friend class ProviderHandle;
    mutable std::atomic<uint32> mReferences{1};
};

/// Shared provider ownership; copies retain without allocation. Mutation requires
/// exclusive access to the handle; distinct handles can be used concurrently.
class ProviderHandle final
{
public:
    /// Creates an empty handle.
    ProviderHandle() noexcept = default;
    /// Adopts the initial reference of a newly allocated provider, or an empty null.
    explicit ProviderHandle(Provider* owned) noexcept;
    /// Releases one reference.
    ~ProviderHandle() noexcept;
    /// Retains the same provider without allocation.
    ProviderHandle(const ProviderHandle& other) noexcept;
    /// Replaces ownership with the same provider as other; self-copy is supported.
    ProviderHandle& operator=(const ProviderHandle& other) noexcept;
    /// Transfers ownership, leaving other empty.
    ProviderHandle(ProviderHandle&& other) noexcept;
    /// Releases previous ownership and transfers other; self-move is supported.
    ProviderHandle& operator=(ProviderHandle&& other) noexcept;
    /// Retains a live provider whose current owner remains alive during this call.
    [[nodiscard]] static ProviderHandle Share(const Provider& provider) noexcept;
    /// Returns the provider, or null. The pointer is borrowed for this handle's lifetime.
    [[nodiscard]] const Provider* Get() const noexcept;

private:
    const Provider* mProvider = nullptr;
};

/// Borrowed memory-provider input; creation copies both path and bytes.
struct MemoryEntry final
{
    std::string_view Path;        ///< ValidPath key, compared byte-exactly.
    std::span<const uint8> Bytes; ///< Immutable source bytes; an empty file is valid.
};

/// Maximum memory-provider entry count, bounding index construction and lookup storage.
inline constexpr usize MAX_MEMORY_FILES = 65536;
/// Copies and sorts entries into an immutable provider available on every target.
/// Duplicate/invalid keys or unrepresentable allocation sizes return InvalidArgument;
/// allocation failure returns OutOfMemory. Failure preserves output. No portable
/// host case/Unicode collision guarantee is implied; pack publishing is F3's boundary.
[[nodiscard]] Result CreateMemoryProvider(std::span<const MemoryEntry> entries, ProviderHandle& output) noexcept;
/// Pins a trusted native root using Directory's containment and platform contract.
/// Failure preserves output; Unsupported on targets without native file I/O.
[[nodiscard]] Result CreateDirectoryProvider(std::string_view nativeRoot, ProviderHandle& output) noexcept;

/// Borrowed input describing one mount. Empty Prefix maps the entire root.
struct Mount final
{
    Root Namespace = Root::Assets; ///< Explicit namespace this mount serves.
    std::string_view Prefix;       ///< Empty or ValidPath prefix; matches complete components.
    int32 Priority = 0;            ///< Higher values resolve first.
    uint64 Id = 0;                 ///< Host-assigned nonzero ID, unique in a snapshot; lower wins ties.
    ProviderHandle Source;         ///< Retained immutable provider revision.
};

/// Borrowed mount description, in deterministic resolution order.
struct MountInfo final
{
    Root Namespace = Root::Assets; ///< Served namespace.
    std::string_view Prefix;       ///< Owned by the snapshot; valid while it remains alive.
    int32 Priority = 0;            ///< Resolution priority.
    uint64 Id = 0;                 ///< Stable host-assigned mount ID.
};

/// One attempted matching mount; callers can inspect overlay collisions and faults.
struct MountAttempt final
{
    uint64 Id = 0;  ///< Mount attempted in priority/ID order.
    Result Outcome; ///< Provider outcome, including native diagnostic code.
};

/// Lookup outcome plus trace length, including attempts beyond the caller's trace capacity.
struct LookupResult final
{
    Result Outcome;     ///< Success, final non-NotFound fault, or NotFound if all candidates miss.
    usize Attempts = 0; ///< Number of matching providers actually tried.
};

class MountSnapshot;

/// Move-only logical file retaining its provider and complete mount snapshot through reload/unmount.
/// Concurrent const operations require stable owners and disjoint destination buffers.
class VirtualFile final
{
public:
    /// Creates an empty file.
    VirtualFile() noexcept = default;
    /// Releases the revision best-effort without logging.
    ~VirtualFile() noexcept;
    /// Use Clone to retain the same immutable opened revision.
    VirtualFile(const VirtualFile&) = delete;
    /// Use Clone to retain the same immutable opened revision.
    VirtualFile& operator=(const VirtualFile&) = delete;
    /// Transfers ownership, leaving other empty.
    VirtualFile(VirtualFile&& other) noexcept;
    /// Releases the previous file and transfers other; self-move is supported.
    VirtualFile& operator=(VirtualFile&& other) noexcept;
    /// Returns whether a revision is open.
    [[nodiscard]] bool IsOpen() const noexcept;
    /// Returns captured bytes, or zero when closed.
    [[nodiscard]] uint64 Size() const noexcept;
    /// Returns the originating mount ID, or zero when closed.
    [[nodiscard]] uint64 MountId() const noexcept;
    /// Returns the originating snapshot generation, or zero when closed.
    [[nodiscard]] uint64 Generation() const noexcept;
    /// Retains the same file without allocation or pathname lookup; failure preserves output.
    /// A closed source returns InvalidArgument. Output may alias this owner.
    [[nodiscard]] Result Clone(VirtualFile& output) const noexcept;
    /// Reads using ProviderFile's offset/EOF/error contract. A closed file returns InvalidArgument.
    [[nodiscard]] ReadResult ReadAt(uint64 offset, std::span<uint8> destination) const noexcept;
    /// Releases ownership; repeated closes are harmless. Provider destruction is best-effort.
    void Close() noexcept;

private:
    friend class MountSnapshot;
    struct Impl;
    Impl* mImpl = nullptr;
};

/// Maximum mount count, bounding snapshot construction and resolution work.
inline constexpr usize MAX_MOUNTS = 256;
/// Immutable, shared read mount table. The host publishes/replaces handles at its own
/// synchronization boundary; no global namespace, implicit writer or live-table mutation.
/// Concurrent const lookups are supported while their handle remains alive.
class MountSnapshot final
{
public:
    /// Creates an invalid snapshot; use Create even for an empty table.
    MountSnapshot() noexcept = default;
    /// Releases this reference; opened files retain their originating snapshot.
    ~MountSnapshot() noexcept;
    /// Retains the same snapshot without allocation.
    MountSnapshot(const MountSnapshot& other) noexcept;
    /// Retains other and releases the previous snapshot; self-copy is supported.
    MountSnapshot& operator=(const MountSnapshot& other) noexcept;
    /// Transfers ownership, leaving other invalid.
    MountSnapshot(MountSnapshot&& other) noexcept;
    /// Releases the previous snapshot and transfers other; self-move is supported.
    MountSnapshot& operator=(MountSnapshot&& other) noexcept;
    /// Copies prefixes and retains providers, sorting by descending priority then ascending ID.
    /// Invalid roots/prefixes, null providers, duplicate/zero IDs, zero generation or too
    /// many mounts return InvalidArgument. Allocation failure returns OutOfMemory.
    /// Failure preserves output; input may borrow from the previous output snapshot.
    [[nodiscard]] static Result
    Create(std::span<const Mount> mounts, uint64 generation, MountSnapshot& output) noexcept;
    /// Returns whether Create succeeded, including an empty table.
    [[nodiscard]] bool IsValid() const noexcept;
    /// Returns the host-assigned generation, or zero for an invalid handle.
    [[nodiscard]] uint64 Generation() const noexcept;
    /// Returns the number of mounted providers.
    [[nodiscard]] usize Count() const noexcept;
    /// Inspects one mount in resolution order; out-of-range failure preserves output.
    [[nodiscard]] bool Describe(usize index, MountInfo& output) const noexcept;
    /// Resolves complete-component prefixes, stripping the prefix before provider lookup.
    /// An exact prefix has no file key and does not match. Only NotFound falls back.
    /// Failure preserves the opened output. Trace writes up to attempts.size() entries;
    /// LookupResult::Attempts reports all attempted providers. Unused trace slots are preserved.
    [[nodiscard]] LookupResult
    OpenRead(const VirtualPath& path, VirtualFile& output, std::span<MountAttempt> attempts = {}) const noexcept;

private:
    struct Impl;
    Impl* mImpl = nullptr;
};
} // namespace ludus::foundation::filesystem
