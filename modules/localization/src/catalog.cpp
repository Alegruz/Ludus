#include <ludus/localization/catalog.hpp>

#include <ludus/foundation/base/byte_order.hpp>
#include <ludus/foundation/strings/utf8.hpp>

#include <atomic>
#include <utility>

// Thanks to James Boer, "A Flexible Text Parsing System", Game Programming
// Gems 2, section 1.17, pp.112-117: validate readable authoring data offline and
// keep the shipping reader small. Thanks to Jason Hughes, "Pointer Patching
// Assets", Game Engine Gems 2, chapter 20, pp.345-357: prepare bounded
// immutable data before publication. We use checked little-endian offsets, not
// the chapter's native pointer patching. Original implementation; review:
// docs/architecture/localization-gems-review.md.

namespace ludus::localization
{
namespace
{
constexpr usize kHeaderBytes = 48;
constexpr uint32 kRecordBytes = 20;
constexpr uint32 kMagic = 0x434f4c4c; // "LLOC" in little endian.
std::atomic<uint64> gNextToken{1};

uint32 ReadWord(std::span<const uint8> bytes, usize offset) noexcept
{
    uint32 value{};
    (void)foundation::TryReadLittleEndian(bytes.subspan(offset, 4), value);
    return value;
}

bool ValidId(std::string_view value) noexcept
{
    // Same grammar as Content::ValidId, without linking content acquisition.
    if (value.empty() || value.size() > kMaxIdentifierBytes || value.front() == '/' || value.back() == '/')
    {
        return false;
    }
    char previous{};
    for (char ch : value)
    {
        if (((ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '-' && ch != '/') ||
            (ch == '/' && previous == '/'))
        {
            return false;
        }
        previous = ch;
    }
    return true;
}

bool AsciiLetter(char ch) noexcept
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

bool ValidLocale(std::string_view value) noexcept
{
    // Project-selected labels with ASCII BCP47 subtag shape. No canonicalization,
    // registry lookup or language negotiation is implied by this narrow codec.
    if (value.empty() || value.size() > kMaxIdentifierBytes)
    {
        return false;
    }
    usize segment{};
    bool primary = true;
    for (char ch : value)
    {
        if (ch == '-')
        {
            if (segment == 0 || (primary && segment < 2))
            {
                return false;
            }
            primary = false;
            segment = 0;
        }
        else if ((!AsciiLetter(ch) && (primary || ch < '0' || ch > '9')) || ++segment > 8)
        {
            return false;
        }
    }
    return segment != 0 && (!primary || segment >= 2);
}

Status NextToken(uint64& output) noexcept
{
    uint64 token = gNextToken.load(std::memory_order_relaxed);
    while (token != ~uint64{0})
    {
        if (gNextToken.compare_exchange_weak(token, token + 1, std::memory_order_relaxed))
        {
            output = token;
            return Status::Ok;
        }
    }
    return Status::TokenExhausted;
}

Status Fail(Diagnostic& diagnostic, Status status, usize offset) noexcept
{
    diagnostic = {status, offset};
    return status;
}
} // namespace

Catalog::Catalog(Catalog&& other) noexcept
    : mBytes(std::move(other.mBytes)), mToken(std::exchange(other.mToken, 0)), mCount(std::exchange(other.mCount, 0)),
      mRecords(std::exchange(other.mRecords, 0))
{
}

Catalog& Catalog::operator=(Catalog&& other) noexcept
{
    if (this != &other)
    {
        mBytes = std::move(other.mBytes);
        mToken = std::exchange(other.mToken, 0);
        mCount = std::exchange(other.mCount, 0);
        mRecords = std::exchange(other.mRecords, 0);
    }
    return *this;
}

bool Catalog::IsValid() const noexcept
{
    return mToken != 0;
}

uint32 Catalog::GetMessageCount() const noexcept
{
    return mCount;
}

uint32 Catalog::Word(usize offset) const noexcept
{
    const auto bytes = mBytes.GetView();
    return ReadWord({reinterpret_cast<const uint8*>(bytes.data()), bytes.size()}, offset);
}

std::string_view Catalog::Slice(usize offset, usize length) const noexcept
{
    return mBytes.GetView().substr(offset, length);
}

std::string_view Catalog::GetDomain() const& noexcept
{
    return IsValid() ? Slice(kHeaderBytes, Word(16)) : std::string_view{};
}

std::string_view Catalog::GetLocale() const& noexcept
{
    return IsValid() ? Slice(kHeaderBytes + Word(16), Word(20)) : std::string_view{};
}

std::string_view Catalog::GetSourceLocale() const& noexcept
{
    return IsValid() ? Slice(kHeaderBytes + Word(16) + Word(20), Word(24)) : std::string_view{};
}

Status Catalog::BindMessage(MessageKey key, MessageBinding& output) const noexcept
{
    if (!IsValid() || !ValidId(key.Domain) || !ValidId(key.Key))
    {
        return Status::Invalid;
    }
    if (key.Domain != GetDomain())
    {
        return Status::NotFound;
    }
    uint32 begin{};
    uint32 end = mCount;
    while (begin < end)
    {
        const uint32 index = begin + (end - begin) / 2;
        const usize record = mRecords + static_cast<usize>(index) * kRecordBytes;
        const auto candidate = Slice(Word(record), Word(record + 4));
        const int order = candidate.compare(key.Key);
        if (order < 0)
        {
            begin = index + 1;
        }
        else if (order > 0)
        {
            end = index;
        }
        else
        {
            output.mToken = mToken;
            output.mIndex = index;
            return Status::Ok;
        }
    }
    return Status::NotFound;
}

Status Catalog::ResolveStatic(MessageBinding binding, ResolvedText& output) const& noexcept
{
    if (!IsValid())
    {
        return Status::Invalid;
    }
    if (binding.mToken != mToken || binding.mIndex >= mCount)
    {
        return Status::WrongCatalog;
    }
    const usize record = mRecords + static_cast<usize>(binding.mIndex) * kRecordBytes;
    output = {Slice(Word(record + 8), Word(record + 12)), static_cast<Origin>(Word(record + 16))};
    return Status::Ok;
}

Status PrepareCatalog(std::span<const uint8> bytes,
                      const foundation::AllocationDomain& domain,
                      Catalog& output,
                      Diagnostic& diagnostic) noexcept
{
    if (bytes.size() > kMaxCatalogBytes)
    {
        return Fail(diagnostic, Status::Limit, 0);
    }
    if (bytes.size() < kHeaderBytes || ReadWord(bytes, 0) != kMagic)
    {
        return Fail(diagnostic, Status::Invalid, 0);
    }
    if (ReadWord(bytes, 4) != 1 || ReadWord(bytes, 28) != kRecordBytes || ReadWord(bytes, 32) != 1)
    {
        return Fail(diagnostic, Status::Unsupported, 4);
    }
    if (ReadWord(bytes, 8) != bytes.size() || ReadWord(bytes, 36) != 0 || ReadWord(bytes, 40) != 0 ||
        ReadWord(bytes, 44) != 0)
    {
        return Fail(diagnostic, Status::Invalid, 8);
    }
    const uint32 count = ReadWord(bytes, 12);
    const uint32 domainLength = ReadWord(bytes, 16);
    const uint32 localeLength = ReadWord(bytes, 20);
    const uint32 sourceLength = ReadWord(bytes, 24);
    if (count > kMaxMessages || domainLength > kMaxIdentifierBytes || localeLength > kMaxIdentifierBytes ||
        sourceLength > kMaxIdentifierBytes)
    {
        return Fail(diagnostic, Status::Limit, 12);
    }
    // Prior bounds make every addition/product safe on native AND wasm32.
    const usize records = kHeaderBytes + domainLength + localeLength + sourceLength;
    usize cursor = records + static_cast<usize>(count) * kRecordBytes;
    if (cursor > bytes.size())
    {
        return Fail(diagnostic, Status::Invalid, 12);
    }
    const std::string_view data{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    const auto locale = data.substr(kHeaderBytes + domainLength, localeLength);
    const auto source = data.substr(kHeaderBytes + domainLength + localeLength, sourceLength);
    if (!ValidId(data.substr(kHeaderBytes, domainLength)) || !ValidLocale(locale) || !ValidLocale(source))
    {
        return Fail(diagnostic, Status::Invalid, kHeaderBytes);
    }
    std::string_view previous;
    for (uint32 index = 0; index < count; ++index)
    {
        const usize record = records + static_cast<usize>(index) * kRecordBytes;
        const uint32 keyOffset = ReadWord(bytes, record);
        const uint32 keyLength = ReadWord(bytes, record + 4);
        const uint32 textOffset = ReadWord(bytes, record + 8);
        const uint32 textLength = ReadWord(bytes, record + 12);
        const uint32 origin = ReadWord(bytes, record + 16);
        if (keyLength > kMaxIdentifierBytes || textLength > kMaxTextBytes)
        {
            return Fail(diagnostic, Status::Limit, record);
        }
        if (keyOffset != cursor || keyLength > bytes.size() - cursor)
        {
            return Fail(diagnostic, Status::Invalid, record);
        }
        const auto key = data.substr(cursor, keyLength);
        if (!ValidId(key) || (index != 0 && previous >= key))
        {
            return Fail(diagnostic, Status::Invalid, keyOffset);
        }
        previous = key;
        cursor += keyLength;
        if (textOffset != cursor || textLength > bytes.size() - cursor ||
            (locale == source ? origin != 0 : (origin != 1 && origin != 2)))
        {
            return Fail(diagnostic, Status::Invalid, record + 8);
        }
        const auto text = data.substr(cursor, textLength);
        foundation::Utf8View validated;
        usize invalidByte{};
        if (foundation::ValidateUtf8(text, validated, invalidByte) != foundation::StringStatus::Ok)
        {
            return Fail(diagnostic, Status::Invalid, cursor + invalidByte);
        }
        const usize zero = text.find('\0');
        if (zero != std::string_view::npos)
        {
            return Fail(diagnostic, Status::Invalid, cursor + zero);
        }
        cursor += textLength;
    }
    if (cursor != bytes.size())
    {
        return Fail(diagnostic, Status::Invalid, cursor);
    }
    Catalog candidate;
    const auto allocation = foundation::CreateShared(data, domain, candidate.mBytes);
    if (allocation != foundation::StringStatus::Ok)
    {
        return Fail(diagnostic, Status::OutOfMemory, 0);
    }
    const Status token = NextToken(candidate.mToken);
    if (token != Status::Ok)
    {
        return Fail(diagnostic, token, 0);
    }
    candidate.mCount = count;
    candidate.mRecords = static_cast<uint32>(records);
    output = std::move(candidate);
    diagnostic = {};
    return Status::Ok;
}
} // namespace ludus::localization
