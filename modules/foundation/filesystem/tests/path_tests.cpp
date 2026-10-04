#include <ludus/foundation/filesystem/filesystem.hpp>

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace ludus::foundation;
using namespace ludus::foundation::filesystem;

TEST_CASE("Filesystem paths reject traversal and malformed UTF-8 without rewriting names")
{
    for (const auto* path : {"", "/a", "a/", "a//b", ".", "..", "a/./b", "a/../b", "a\\b", "C:a", "a\n", "a\x7f"})
    {
        INFO(path);
        REQUIRE_FALSE(ValidPath(path));
    }
    for (const auto* path : {"a", "a/b.bin", "audio/音.wav", "a b/file", "...", ".hidden"})
    {
        INFO(path);
        REQUIRE(ValidPath(path));
    }
    for (const std::string_view path : {std::string_view("a\0b", 3),
                                        std::string_view("a\xff", 2),
                                        std::string_view("a\xc0\x80", 3),
                                        std::string_view("a\xed\xa0\x80", 4),
                                        std::string_view("a\xf4\x90\x80\x80", 5),
                                        std::string_view("a\xe2\x82", 3)})
    {
        REQUIRE_FALSE(ValidPath(path));
    }
    REQUIRE(ValidPath(std::string(MAX_PATH_BYTES, 'a')));
    REQUIRE_FALSE(ValidPath(std::string(MAX_PATH_BYTES + 1, 'a')));
}
