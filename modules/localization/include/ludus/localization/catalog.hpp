#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/memory/allocation_domain.hpp>
#include <ludus/foundation/strings/shared_string.hpp>

#include <span>
#include <string_view>

namespace ludus::localization
{
using foundation::uint32;
using foundation::uint64;
using foundation::uint8;
using foundation::usize;

inline constexpr usize kMaxCatalogBytes = usize{32} * 1024 * 1024;
inline constexpr uint32 kMaxMessages = 65536;
inline constexpr usize kMaxTextBytes = usize{64} * 1024;
inline constexpr usize kMaxIdentifierBytes = 128;

enum class Status : uint8
{
    Ok,
    Invalid,
    Unsupported,
    Limit,
    OutOfMemory,
    NotFound,
    WrongCatalog,
    TokenExhausted,
};

struct Diagnostic final
{
    Status Result{Status::Ok};
    usize Offset{};
};

// Exact authored identity; neither dictionary indices nor hashes are persistent.
struct MessageKey final
{
    std::string_view Domain;
    std::string_view Key;
};

class MessageBinding final
{
private:
    uint64 mToken{};
    uint32 mIndex{};
    friend class Catalog;
};

enum class Origin : uint8
{
    Source,
    Translation,
    SourceFallback,
};

struct ResolvedText final
{
    std::string_view Text;
    Origin Provenance{Origin::Source};
};

// Immutable snapshot AND lifetime lease. Copies share one allocation. Separate
// copies may be read/destroyed on different threads after synchronized publication.
// The allocation domain must outlive every copy. Borrowed views require a live
// lease; assignment/destruction of the last lease invalidates them.
//
// Each successful prepare gets a new token, even for identical schemas/locales.
// Rebind after replacement; copies/moves retain the token. There is no global
// current language, implicit fallback, or cross-catalog binding compatibility.
class Catalog final
{
public:
    Catalog() noexcept = default;
    ~Catalog() noexcept = default;
    Catalog(const Catalog&) noexcept = default;
    Catalog& operator=(const Catalog&) noexcept = default;
    Catalog(Catalog&& other) noexcept;
    Catalog& operator=(Catalog&& other) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] uint32 GetMessageCount() const noexcept;
    [[nodiscard]] std::string_view GetDomain() const& noexcept;
    [[nodiscard]] std::string_view GetDomain() const&& = delete;
    [[nodiscard]] std::string_view GetLocale() const& noexcept;
    [[nodiscard]] std::string_view GetLocale() const&& = delete;
    [[nodiscard]] std::string_view GetSourceLocale() const& noexcept;
    [[nodiscard]] std::string_view GetSourceLocale() const&& = delete;

    // Cold exact-key binary search; outputs are unchanged on any failure.
    [[nodiscard]] Status BindMessage(MessageKey key, MessageBinding& output) const noexcept;
    // O(1), zero allocation. Literal UTF-8; braces/apostrophes are not patterns.
    [[nodiscard]] Status ResolveStatic(MessageBinding binding, ResolvedText& output) const& noexcept;
    [[nodiscard]] Status ResolveStatic(MessageBinding, ResolvedText&) const&& = delete;

private:
    foundation::SharedString mBytes;
    uint64 mToken{};
    uint32 mCount{};
    uint32 mRecords{};
    [[nodiscard]] uint32 Word(usize offset) const noexcept;
    [[nodiscard]] std::string_view Slice(usize offset, usize length) const noexcept;
    friend Status
    PrepareCatalog(std::span<const uint8>, const foundation::AllocationDomain&, Catalog&, Diagnostic&) noexcept;
};

// Validates the portable static-UTF8 v1 format and copies the caller's bytes.
// Transactional, including an input that borrows the previous output. No I/O.
// Caller must not mutate input concurrently. Failure preserves output; diagnostic
// reports a byte offset, including malformed cooked files without allocating.
[[nodiscard]] Status PrepareCatalog(std::span<const uint8> bytes,
                                    const foundation::AllocationDomain& domain,
                                    Catalog& output,
                                    Diagnostic& diagnostic) noexcept;
} // namespace ludus::localization
