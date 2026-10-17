#include <ludus/foundation/identity/uuid.hpp>

#include <concepts>
#include <cstring>
#include <string_view>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
using namespace ludus::foundation::identity;

// Semantic UUID wrappers are distinct types with no implicit conversion.
static_assert(std::is_convertible_v<AssetId, SourceObjectId> == false);
static_assert(std::is_constructible_v<InstanceId, SourceObjectId> == false);
static_assert(std::is_convertible_v<Uuid, AssetId> == false);
static_assert(std::is_convertible_v<AssetId, Uuid> == false);
static_assert(std::equality_comparable_with<AssetId, InstanceId> == false);
static_assert(std::is_trivially_copyable_v<Uuid>);
static_assert(std::is_trivially_copyable_v<PersistentObjectKey>);
static_assert(sizeof(AssetId) == UUID_BYTE_COUNT);
static_assert(Uuid{}.IsNil() == true);

namespace
{
/// RFC 9562 appendix A.3 UUIDv4 example.
constexpr std::string_view RFC_V4_TEXT = "919108f7-52d1-4320-9bac-f847db4148a8";
constexpr Uuid RFC_V4_VALUE = {
    {0x91, 0x91, 0x08, 0xf7, 0x52, 0xd1, 0x43, 0x20, 0x9b, 0xac, 0xf8, 0x47, 0xdb, 0x41, 0x48, 0xa8}};
/// RFC 9562 appendix A.6 UUIDv7 example.
constexpr std::string_view RFC_V7_TEXT = "017f22e2-79b0-7cc3-98c4-dc0c0c07398f";
constexpr std::string_view NIL_TEXT = "00000000-0000-0000-0000-000000000000";
/// RFC 9562 section 5.10 Max UUID: variant bits 11 are not the standard variant.
constexpr std::string_view MAX_TEXT = "ffffffff-ffff-ffff-ffff-ffffffffffff";
/// A recognizable value that every failed operation must leave untouched.
constexpr Uuid SENTINEL = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};

[[nodiscard]] UuidStatus Parse(std::string_view text, UuidPolicy policy, Uuid& outValue)
{
    return TryParseUuid(text.data(), text.size(), policy, outValue);
}

struct FixedEntropy final
{
    uint8 Fill = 0;
    uint32 CallCount = 0;
};

bool FillFixed(void* contextOrNull, uint8* outBytes, usize size) noexcept
{
    FixedEntropy& entropy = *static_cast<FixedEntropy*>(contextOrNull);
    ++entropy.CallCount;
    for (usize i = 0; i < size; ++i)
    {
        outBytes[i] = static_cast<uint8>(entropy.Fill + i);
    }
    return true;
}

bool FillPartiallyThenFail(void* contextOrNull, uint8* outBytes, usize size) noexcept
{
    static_cast<void>(contextOrNull);
    // Scribble on the destination to prove failed output never escapes.
    for (usize i = 0; i < size / 2; ++i)
    {
        outBytes[i] = 0xEE;
    }
    return false;
}
} // namespace

TEST_CASE("RFC 9562 version-4 example parses and formats round trip", "[identity][uuid]")
{
    Uuid value = SENTINEL;
    REQUIRE(Parse(RFC_V4_TEXT, UuidPolicy::RequireVersion4, value) == UuidStatus::Success);
    CHECK(value == RFC_V4_VALUE);
    CHECK(value.GetVersion() == UUID_VERSION_RANDOM);
    CHECK(value.HasStandardVariant() == true);
    CHECK(ValidateUuid(value, UuidPolicy::RequireVersion4) == UuidStatus::Success);

    char text[UUID_TEXT_BUFFER_BYTES];
    std::memset(text, 'x', sizeof(text));
    REQUIRE(FormatUuid(value, text, sizeof(text)) == UuidStatus::Success);
    CHECK(std::string_view(text) == RFC_V4_TEXT);
}

TEST_CASE("parsing is case-insensitive and formatting is lowercase", "[identity][uuid]")
{
    Uuid value;
    REQUIRE(Parse("919108F7-52D1-4320-9BAC-F847DB4148A8", UuidPolicy::RequireVersion4, value) == UuidStatus::Success);
    CHECK(value == RFC_V4_VALUE);
    REQUIRE(Parse("919108f7-52D1-4320-9bAc-f847db4148A8", UuidPolicy::RequireVersion4, value) == UuidStatus::Success);
    CHECK(value == RFC_V4_VALUE);
    char text[UUID_TEXT_LENGTH];
    REQUIRE(FormatUuid(value, text, sizeof(text)) == UuidStatus::Success);
    CHECK(std::string_view(text, sizeof(text)) == RFC_V4_TEXT);
}

TEST_CASE("binary round trip through text preserves every byte", "[identity][uuid]")
{
    Uuid value;
    for (usize i = 0; i < UUID_BYTE_COUNT; ++i)
    {
        value.Bytes[i] = static_cast<uint8>(0x10 * i + 0x0F - i);
    }
    // Arbitrary bytes format; only parsing applies the policy.
    char text[UUID_TEXT_LENGTH];
    REQUIRE(FormatUuid(value, text, sizeof(text)) == UuidStatus::Success);
    Uuid made;
    REQUIRE(TryMakeUuidV4(value.Bytes, UUID_BYTE_COUNT, made) == UuidStatus::Success);
    REQUIRE(FormatUuid(made, text, sizeof(text)) == UuidStatus::Success);
    Uuid parsed = SENTINEL;
    REQUIRE(TryParseUuid(text, sizeof(text), UuidPolicy::RequireVersion4, parsed) == UuidStatus::Success);
    CHECK(parsed == made);
}

TEST_CASE("malformed text is rejected and leaves the output unchanged", "[identity][uuid]")
{
    constexpr std::string_view MALFORMED[] = {
        "",
        "919108f7-52d1-4320-9bac-f847db4148a",    // short
        "919108f7-52d1-4320-9bac-f847db4148a8a",  // long
        "919108f752d143209bacf847db4148a8",       // no separators
        "{919108f7-52d1-4320-9bac-f847db4148a8}", // braces
        "urn:uuid:919108f7-52d1-4320-9bac-f847db4148a8",
        " 919108f7-52d1-4320-9bac-f847db4148a8", // leading whitespace
        "919108f7-52d1-4320-9bac-f847db4148a8 ", // trailing whitespace
        "919108f7-52d1-4320-9bac-f847db4148a ",  // whitespace in place of a digit
        "919108f7_52d1-4320-9bac-f847db4148a8",  // wrong separator
        "919108f7-52d14-320-9bac-f847db4148a8",  // separator moved
        "919108g7-52d1-4320-9bac-f847db4148a8",  // non-hex digit
        "919108f7-52d1-4320-9bac-f847db4148-8",  // separator in place of a digit
        "+19108f7-52d1-4320-9bac-f847db4148a8",  // sign
        "0x9108f7-52d1-4320-9bac-f847db4148a8",  // radix prefix
    };
    for (const std::string_view text : MALFORMED)
    {
        Uuid value = SENTINEL;
        CAPTURE(text);
        CHECK(Parse(text, UuidPolicy::AllowNilOrVersion4, value) == UuidStatus::MalformedInput);
        CHECK(value == SENTINEL);
    }

    // An embedded terminator inside the declared size is not accepted.
    char embedded[UUID_TEXT_LENGTH];
    std::memcpy(embedded, RFC_V4_TEXT.data(), UUID_TEXT_LENGTH);
    embedded[20] = '\0';
    Uuid value = SENTINEL;
    CHECK(TryParseUuid(embedded, sizeof(embedded), UuidPolicy::RequireVersion4, value) == UuidStatus::MalformedInput);
    CHECK(value == SENTINEL);
    CHECK(TryParseUuid(nullptr, 0, UuidPolicy::RequireVersion4, value) == UuidStatus::MalformedInput);
    CHECK(value == SENTINEL);
}

TEST_CASE("nil is an optional-reference sentinel, never an identity", "[identity][uuid]")
{
    Uuid value = SENTINEL;
    CHECK(Parse(NIL_TEXT, UuidPolicy::RequireVersion4, value) == UuidStatus::NilIdentity);
    CHECK(value == SENTINEL);
    REQUIRE(Parse(NIL_TEXT, UuidPolicy::AllowNilOrVersion4, value) == UuidStatus::Success);
    CHECK(value.IsNil() == true);
    CHECK(ValidateUuid(Uuid{}, UuidPolicy::RequireVersion4) == UuidStatus::NilIdentity);
    CHECK(ValidateUuid(Uuid{}, UuidPolicy::AllowNilOrVersion4) == UuidStatus::Success);
    CHECK(AssetId{}.IsNil() == true);
    CHECK(AssetId{RFC_V4_VALUE}.IsNil() == false);
}

TEST_CASE("unsupported versions and variants are rejected", "[identity][uuid]")
{
    Uuid value = SENTINEL;
    CHECK(Parse(RFC_V7_TEXT, UuidPolicy::AllowNilOrVersion4, value) == UuidStatus::UnsupportedVersion);
    CHECK(Parse(MAX_TEXT, UuidPolicy::AllowNilOrVersion4, value) == UuidStatus::UnsupportedVariant);
    // Version 4 bits with a Microsoft (110) variant.
    CHECK(Parse("919108f7-52d1-4320-cbac-f847db4148a8", UuidPolicy::RequireVersion4, value) ==
          UuidStatus::UnsupportedVariant);
    // Standard variant with each nonrandom version nibble.
    for (uint8 version = 0; version < 16; ++version)
    {
        Uuid candidate = RFC_V4_VALUE;
        candidate.Bytes[UUID_VERSION_BYTE_INDEX] =
            static_cast<uint8>((version << 4) | (candidate.Bytes[UUID_VERSION_BYTE_INDEX] & 0x0F));
        const UuidStatus expected =
            version == UUID_VERSION_RANDOM ? UuidStatus::Success : UuidStatus::UnsupportedVersion;
        CAPTURE(version);
        CHECK(ValidateUuid(candidate, UuidPolicy::RequireVersion4) == expected);
    }
    CHECK(value == SENTINEL);
}

TEST_CASE("formatting reports insufficient space without writing", "[identity][uuid]")
{
    char text[UUID_TEXT_BUFFER_BYTES];
    std::memset(text, 'x', sizeof(text));
    CHECK(FormatUuid(RFC_V4_VALUE, text, UUID_TEXT_LENGTH - 1) == UuidStatus::InsufficientSpace);
    for (const char character : text)
    {
        CHECK(character == 'x');
    }
    CHECK(FormatUuid(RFC_V4_VALUE, nullptr, 0) == UuidStatus::InsufficientSpace);

    // Exactly 36 bytes: no terminator is written past the text.
    REQUIRE(FormatUuid(RFC_V4_VALUE, text, UUID_TEXT_LENGTH) == UuidStatus::Success);
    CHECK(text[UUID_TEXT_LENGTH] == 'x');
    REQUIRE(FormatUuid(RFC_V4_VALUE, text, UUID_TEXT_BUFFER_BYTES) == UuidStatus::Success);
    CHECK(text[UUID_TEXT_LENGTH] == '\0');
}

TEST_CASE("version-4 construction sets only the version and variant fields", "[identity][uuid]")
{
    uint8 bytes[UUID_BYTE_COUNT];
    std::memset(bytes, 0xFF, sizeof(bytes));
    Uuid value;
    REQUIRE(TryMakeUuidV4(bytes, sizeof(bytes), value) == UuidStatus::Success);
    CHECK(value.Bytes[UUID_VERSION_BYTE_INDEX] == 0x4F);
    CHECK(value.Bytes[UUID_VARIANT_BYTE_INDEX] == 0xBF);
    std::memset(bytes, 0x00, sizeof(bytes));
    REQUIRE(TryMakeUuidV4(bytes, sizeof(bytes), value) == UuidStatus::Success);
    CHECK(value.Bytes[UUID_VERSION_BYTE_INDEX] == 0x40);
    CHECK(value.Bytes[UUID_VARIANT_BYTE_INDEX] == 0x80);
    // All-zero input still yields a valid nonnil identity.
    CHECK(value.IsNil() == false);
    CHECK(ValidateUuid(value, UuidPolicy::RequireVersion4) == UuidStatus::Success);
    for (usize i = 0; i < UUID_BYTE_COUNT; ++i)
    {
        if (i != UUID_VERSION_BYTE_INDEX && i != UUID_VARIANT_BYTE_INDEX)
        {
            CHECK(value.Bytes[i] == 0);
        }
    }

    value = SENTINEL;
    CHECK(TryMakeUuidV4(bytes, UUID_BYTE_COUNT - 1, value) == UuidStatus::MalformedInput);
    CHECK(TryMakeUuidV4(bytes, UUID_BYTE_COUNT + 1, value) == UuidStatus::MalformedInput);
    CHECK(TryMakeUuidV4(nullptr, 0, value) == UuidStatus::MalformedInput);
    CHECK(value == SENTINEL);
}

TEST_CASE("generation uses one entropy call and reports failure without partial output", "[identity][uuid]")
{
    FixedEntropy entropy{ .Fill = 0xA0, .CallCount = 0 };
    const EntropySource source{ .FillBytesOrNull = FillFixed, .ContextOrNull = &entropy };
    Uuid value;
    REQUIRE(TryGenerateUuidV4(source, value) == UuidStatus::Success);
    CHECK(entropy.CallCount == 1);
    CHECK(ValidateUuid(value, UuidPolicy::RequireVersion4) == UuidStatus::Success);
    CHECK(value.Bytes[0] == 0xA0);
    CHECK(value.Bytes[15] == 0xAF);

    value = SENTINEL;
    CHECK(TryGenerateUuidV4(EntropySource{}, value) == UuidStatus::EntropyUnavailable);
    CHECK(value == SENTINEL);
    const EntropySource failing{ .FillBytesOrNull = FillPartiallyThenFail, .ContextOrNull = nullptr };
    CHECK(TryGenerateUuidV4(failing, value) == UuidStatus::EntropyUnavailable);
    CHECK(value == SENTINEL);
}

TEST_CASE("an injected duplicate entropy stream yields equal UUIDs", "[identity][uuid]")
{
    // Uniqueness is probabilistic; index owners detect duplicates by exact equality.
    FixedEntropy entropy{ .Fill = 0x33, .CallCount = 0 };
    const EntropySource source{ .FillBytesOrNull = FillFixed, .ContextOrNull = &entropy };
    Uuid first;
    Uuid second;
    REQUIRE(TryGenerateUuidV4(source, first) == UuidStatus::Success);
    REQUIRE(TryGenerateUuidV4(source, second) == UuidStatus::Success);
    CHECK(first == second);
    Uuid differentLastByte = second;
    differentLastByte.Bytes[UUID_BYTE_COUNT - 1] ^= 1;
    CHECK(first != differentLastByte);
}

TEST_CASE("persistent object keys compare instance and object", "[identity][uuid]")
{
    Uuid other = RFC_V4_VALUE;
    other.Bytes[0] = 0x00;
    const InstanceId firstPlacement{RFC_V4_VALUE};
    const InstanceId secondPlacement{other};
    const SourceObjectId target{RFC_V4_VALUE};
    // The same source object in two placements names two different objects.
    const PersistentObjectKey first{firstPlacement, target};
    const PersistentObjectKey second{secondPlacement, target};
    CHECK(first != second);
    CHECK(first == PersistentObjectKey{firstPlacement, target});
    CHECK(first.Object == second.Object);
}
