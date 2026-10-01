// Strict UTF-8 validation tests (requirement T04): overlong encodings, invalid
// continuation bytes, surrogates, and > U+10FFFF, reporting the first invalid
// byte. The validator is private, so the tests include it directly.

#include "internal/utf8.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using ludus::text::internal::ValidateUtf8;

TEST_CASE("Valid ASCII and multibyte sequences pass", "[text][utf8]")
{
    CHECK(ValidateUtf8("").Valid);
    CHECK(ValidateUtf8("hello").Valid);
    CHECK(ValidateUtf8("café").Valid);             // 2-byte é
    CHECK(ValidateUtf8("한글").Valid);             // 3-byte Hangul
    CHECK(ValidateUtf8("\xF0\x9F\x98\x80").Valid); // 4-byte emoji U+1F600

    auto counted = ValidateUtf8("a한\xF0\x9F\x98\x80");
    CHECK(counted.Valid);
    CHECK(counted.CodePointCount == 3);
}

TEST_CASE("Overlong encodings are rejected", "[text][utf8]")
{
    // 0xC0 0x80 is an overlong NUL.
    auto r = ValidateUtf8(std::string_view("\xC0\x80", 2));
    CHECK_FALSE(r.Valid);
    CHECK(r.FirstInvalidByte == 0);

    // Overlong 3-byte for '/': 0xE0 0x80 0xAF.
    auto r3 = ValidateUtf8(std::string_view("\xE0\x80\xAF", 3));
    CHECK_FALSE(r3.Valid);
}

TEST_CASE("Invalid continuation bytes report the right offset", "[text][utf8]")
{
    // Valid 'a', then a lead byte with a bad continuation.
    auto r = ValidateUtf8(std::string_view("a\xE0\x28\x8F", 4));
    CHECK_FALSE(r.Valid);
    CHECK(r.FirstInvalidByte == 1);
}

TEST_CASE("Surrogate code points are rejected", "[text][utf8]")
{
    // U+D800 encoded as 0xED 0xA0 0x80.
    auto r = ValidateUtf8(std::string_view("\xED\xA0\x80", 3));
    CHECK_FALSE(r.Valid);
    CHECK(r.FirstInvalidByte == 0);
}

TEST_CASE("Code points above U+10FFFF are rejected", "[text][utf8]")
{
    // 0xF4 0x90 0x80 0x80 == U+110000, just out of range.
    auto r = ValidateUtf8(std::string_view("\xF4\x90\x80\x80", 4));
    CHECK_FALSE(r.Valid);

    // 0xF5.. is never a valid lead byte.
    auto r5 = ValidateUtf8(std::string_view("\xF5\x80\x80\x80", 4));
    CHECK_FALSE(r5.Valid);
}

TEST_CASE("Stray continuation byte is rejected", "[text][utf8]")
{
    auto r = ValidateUtf8(std::string_view("\x80", 1));
    CHECK_FALSE(r.Valid);
    CHECK(r.FirstInvalidByte == 0);
}

TEST_CASE("Truncated multibyte sequence at end is rejected", "[text][utf8]")
{
    auto r = ValidateUtf8(std::string_view("abc\xE0\x80", 5)); // missing 3rd byte
    CHECK_FALSE(r.Valid);
    CHECK(r.FirstInvalidByte == 3);
}
