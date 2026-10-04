#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/hash/hash.hpp>
#include <ludus/foundation/memory/allocation_domain.hpp>
#include <ludus/foundation/strings/status.hpp>

#include <string_view>

namespace ludus::foundation
{
// Index is meaningful only inside its enclosing table/snapshot. Never persist
// a runtime NameId or compare indices across dictionaries.
struct StringIndex final
{
    uint32 Value{0xffffffffU};
    friend constexpr bool operator==(StringIndex, StringIndex) noexcept = default;
};
struct NameId final
{
    uint32 TableToken{};
    StringIndex Index;
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return TableToken != 0 && Index.Value != 0xffffffffU;
    }
    friend constexpr bool operator==(NameId, NameId) noexcept = default;
};
struct NameLiteral final
{
    std::string_view Spelling;
    uint64 Fingerprint;
    // Source must remain alive until binding. consteval does not prove lifetime.
    template <usize N>
    consteval explicit NameLiteral(const char (&spelling)[N]) noexcept
        : Spelling(spelling, N - 1), Fingerprint(SymbolFingerprint64(Spelling))
    {
    }
};
enum class StringHashPolicy : uint8
{
    Trusted,
    Untrusted
};
struct StringTableConfig final
{
    usize MaxSpellingBytes{4096};
    uint32 MaxEntries{65536};                        // includes empty entry at index zero
    usize MaxAllocatedBytes{usize{8} * 1024 * 1024}; // includes rehash/growth peak
    StringHashPolicy HashPolicy{StringHashPolicy::Trusted};
    SipHashKey Key{};
    bool HasSecretKey{}; // caller acquired a secret from platform entropy
    bool RequireUtf8{true};
    bool RejectEmbeddedZero{true};
};
class FrozenStringTable;
class StringTable final
{
public:
    StringTable() noexcept = default;
    ~StringTable() noexcept;
    StringTable(StringTable&& other) noexcept;
    StringTable& operator=(StringTable&& other) noexcept;
    StringTable(const StringTable&) = delete;
    StringTable& operator=(const StringTable&) = delete;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return mState != nullptr;
    }
    [[nodiscard]] StringStatus TryIntern(std::string_view value, NameId& output) noexcept;
    [[nodiscard]] StringStatus TryFind(std::string_view value, NameId& output) const noexcept;
    [[nodiscard]] StringStatus TryResolve(NameId id, std::string_view& output) const& noexcept;
    StringStatus TryResolve(NameId, std::string_view&) const&& = delete;
    // Caller quiesces ALL concurrent operations. Consumes this table and keeps
    // every token/index stable; output must be empty. No allocation required.
    [[nodiscard]] StringStatus TryFreeze(FrozenStringTable& output) noexcept;

private:
    struct State;
    State* mState{};
    friend class FrozenStringTable;
    friend StringStatus CreateTable(const StringTableConfig&, const AllocationDomain&, StringTable&) noexcept;
    friend struct StringTableTestAccess;
};
class FrozenStringTable final
{
public:
    FrozenStringTable() noexcept = default;
    ~FrozenStringTable() noexcept;
    FrozenStringTable(FrozenStringTable&& other) noexcept;
    FrozenStringTable& operator=(FrozenStringTable&& other) noexcept;
    FrozenStringTable(const FrozenStringTable&) = delete;
    FrozenStringTable& operator=(const FrozenStringTable&) = delete;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return mState != nullptr;
    }
    [[nodiscard]] StringStatus TryFind(std::string_view value, NameId& output) const noexcept;
    [[nodiscard]] StringStatus TryResolve(NameId id, std::string_view& output) const& noexcept;
    StringStatus TryResolve(NameId, std::string_view&) const&& = delete;

private:
    StringTable::State* mState{};
    friend class StringTable;
};
// Empty output required; failure preserves output. The runtime token issuer
// must be linked once into the host; plugins use the host's table API.
[[nodiscard]] StringStatus
CreateTable(const StringTableConfig& config, const AllocationDomain& domain, StringTable& output) noexcept;
} // namespace ludus::foundation
