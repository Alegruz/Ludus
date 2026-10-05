#include <ludus/foundation/parsing/parsing.hpp>

#include <span>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;
using namespace ludus::foundation::parsing;

TEST_CASE("Byte cursor is transactional across truncated and unaligned records", "[parsing]")
{
    const uint8 bytes[] = {0xff, 0x78, 0x56, 0x34, 0x12, 1, 2, 3, 4, 5, 6, 7, 8};
    ByteCursor cursor(bytes);
    uint8 tag = 0;
    REQUIRE(cursor.ReadUint8(tag));
    CHECK(tag == 0xff);
    uint64 wide = 99;
    uint32 word = 0;
    REQUIRE(cursor.ReadUint32LittleEndian(word));
    CHECK(word == 0x12345678);
    REQUIRE(cursor.ReadUint64LittleEndian(wide));
    CHECK(wide == 0x0807060504030201);
    CHECK(cursor.Remaining() == 0);
    CHECK_FALSE(cursor.ReadUint8(tag));
    CHECK(tag == 0xff);
    CHECK_FALSE(cursor.ReadUint32LittleEndian(word));
    CHECK(word == 0x12345678);
    CHECK_FALSE(cursor.ReadUint64LittleEndian(wide));
    CHECK(wide == 0x0807060504030201);
    CHECK(cursor.Position() == sizeof(bytes));

    ByteCursor shortRecord(std::span<const uint8>{bytes + 1, 3});
    std::span<const uint8> view(bytes, 1);
    CHECK_FALSE(shortRecord.Take(UNKNOWN_BYTE_OFFSET, view));
    CHECK(view.data() == bytes);
    CHECK_FALSE(shortRecord.Skip(4));
    CHECK_FALSE(shortRecord.ReadUint32LittleEndian(word));
    CHECK(shortRecord.Position() == 0);
    REQUIRE(shortRecord.Take(3, view));
    CHECK(view.data() == bytes + 1);
    CHECK(view.size() == 3);
    CHECK(shortRecord.Skip(0));
    ByteCursor empty({});
    CHECK(empty.Take(0, view));
    CHECK(view.empty());
}

TEST_CASE("UTF-8 admits scalar boundaries and rejects malformed encodings", "[parsing]")
{
    const std::string_view valid[] = {
        {},
        {"a\0b", 3},
        "\x7f",
        "\xc2\x80",
        "\xdf\xbf",
        "\xe0\xa0\x80",
        "\xed\x9f\xbf",
        "\xee\x80\x80",
        "\xef\xbf\xbf",
        "\xf0\x90\x80\x80",
        "\xf4\x8f\xbf\xbf",
    };
    ParseError error;
    for (auto input : valid)
    {
        REQUIRE(ValidateUtf8(input, error) == ParseStatus::Ok);
        CHECK(error.Status == ParseStatus::Ok);
        CHECK(error.Offset == UNKNOWN_BYTE_OFFSET);
    }
    const std::string_view invalid[] = {
        "\x80",
        "\xc0\x80",
        "\xc1\xbf",
        "\xc2",
        "\xe0\x9f\xbf",
        "\xed\xa0\x80",
        "\xef\xbf",
        "\xf0\x8f\xbf\xbf",
        "\xf4\x90\x80\x80",
        "\xf5\x80\x80\x80",
        "\xff",
        "\xc2x",
    };
    for (auto input : invalid)
    {
        REQUIRE(ValidateUtf8(input, error) == ParseStatus::InvalidEncoding);
        CHECK(error.Offset == 0);
    }
    REQUIRE(ValidateUtf8("ok\xed\xa0\x80", error) == ParseStatus::InvalidEncoding);
    CHECK(error.Offset == 2);
}

TEST_CASE("Locations use byte columns and one CRLF newline", "[parsing]")
{
    constexpr std::string_view text = "a\r\n\xc2\xa2\n\rZ";
    SourcePosition position;
    REQUIRE(LocateByte(text, 5, position));
    CHECK(position.Line == 2);
    CHECK(position.ByteColumn == 3);
    REQUIRE(LocateByte(text, text.size(), position));
    CHECK(position.Line == 4);
    CHECK(position.ByteColumn == 2);
    CHECK_FALSE(LocateByte(text, UNKNOWN_BYTE_OFFSET, position));
    CHECK(position.Line == 4);
    CHECK(position.ByteColumn == 2);
    REQUIRE(LocateByte({}, 0, position));
    CHECK(position.Line == 1);
    CHECK(position.ByteColumn == 1);
}
