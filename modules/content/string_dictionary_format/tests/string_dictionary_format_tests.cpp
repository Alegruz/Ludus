// Header self-sufficiency and layout contract for Ludus::StringDictionaryFormat.

#include <ludus/content/string_dictionary_format/lustr.h>

#include <catch2/catch_test_macros.hpp>

namespace format = ludus::content::string_dictionary_format;

TEST_CASE("LUSTR header and directory offsets match the v1 wire format", "[string_dictionary_format]")
{
    REQUIRE(format::MAGIC.size() == 8);
    REQUIRE(format::MAGIC.back() == '\0');
    REQUIRE(format::VERSION_OFFSET == 8);
    REQUIRE(format::BYTE_POLICY_OFFSET == 12);
    REQUIRE(format::ENTRY_COUNT_OFFSET == 16);
    REQUIRE(format::DIRECTORY_BYTES_OFFSET == 20);
    REQUIRE(format::BLOB_BYTES_OFFSET == 24);
    REQUIRE(format::RESERVED_OFFSET == 28);
    REQUIRE(format::REVISION_DIGEST_OFFSET == 32);
    REQUIRE(format::HEADER_BYTES == 64);
    REQUIRE(format::ENTRY_LENGTH_FIELD == 4);
    REQUIRE(format::DIRECTORY_ENTRY_BYTES == 8);
    REQUIRE(format::EMPTY_ENTRY == 0);
    REQUIRE(format::BYTE_POLICY_UTF8 != format::BYTE_POLICY_BYTES);
}
