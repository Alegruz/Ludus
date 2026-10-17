#include <ludus/foundation/base/types.h>
#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/identity/uuid.hpp>
#include <ludus/platform/entropy.h>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
using namespace ludus::foundation::identity;

namespace
{
/// Number of UUIDs drawn to check that generation does not repeat values.
constexpr usize GENERATED_UUID_COUNT = 64;
/// Spans several 256-byte getentropy(3) requests and ends in a partial one.
constexpr usize LARGE_REQUEST_BYTES = 1000;
/// Bytes compared at the tail of a large request; 2048 zero bits occur by chance with negligible probability.
constexpr usize TAIL_CHECK_BYTES = 256;
/// A recognizable byte that requests must overwrite or, for empty requests, preserve.
constexpr uint8 SENTINEL_BYTE = 0xA5;

[[nodiscard]] bool IsRangeAllZero(const uint8* bytes, usize begin, usize end) noexcept
{
    for (usize i = begin; i < end; ++i)
    {
        if (bytes[i] != 0)
        {
            return false;
        }
    }
    return true;
}
} // namespace

TEST_CASE("The system entropy source is a stateless production adapter")
{
    const EntropySource source = ludus::platform::GetSystemEntropySource();
    CHECK(source.FillBytesOrNull != nullptr);
    CHECK(source.ContextOrNull == nullptr);
}

TEST_CASE("System entropy produces valid distinct version-4 UUIDs")
{
    const EntropySource source = ludus::platform::GetSystemEntropySource();
    StaticArray<Uuid, GENERATED_UUID_COUNT> values{};
    for (usize i = 0; i < GENERATED_UUID_COUNT; ++i)
    {
        Uuid value;
        REQUIRE(TryGenerateUuidV4(source, value) == UuidStatus::Success);
        CHECK(value.IsNil() == false);
        CHECK(value.GetVersion() == UUID_VERSION_RANDOM);
        CHECK(value.HasStandardVariant() == true);
        CHECK(ValidateUuid(value, UuidPolicy::RequireVersion4) == UuidStatus::Success);
        for (usize j = 0; j < i; ++j)
        {
            CHECK((values[j] == value) == false);
        }
        values[i] = value;
    }
}

TEST_CASE("System entropy fills requests larger than one operating-system call")
{
    StaticArray<uint8, LARGE_REQUEST_BYTES> bytes{};
    REQUIRE(ludus::platform::TryFillSystemEntropy(bytes.GetData(), bytes.GetSize()) == true);
    CHECK(IsRangeAllZero(bytes.GetData(), 0, TAIL_CHECK_BYTES) == false);
    CHECK(IsRangeAllZero(bytes.GetData(), LARGE_REQUEST_BYTES - TAIL_CHECK_BYTES, LARGE_REQUEST_BYTES) == false);
}

TEST_CASE("System entropy validates its destination")
{
    CHECK(ludus::platform::TryFillSystemEntropy(nullptr, UUID_BYTE_COUNT) == false);
    CHECK(ludus::platform::TryFillSystemEntropy(nullptr, 0) == false);

    uint8 byte = SENTINEL_BYTE;
    CHECK(ludus::platform::TryFillSystemEntropy(&byte, 0) == true);
    CHECK(byte == SENTINEL_BYTE);
}
