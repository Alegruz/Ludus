#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <ludus/content/content.h>
#include <ludus/content/json.h>
#include <string>
#include <unistd.h>

using namespace ludus::content;
TEST_CASE("Catalog preserves identity and rejects malformed replacements")
{
    Catalog catalog;
    Diagnostic d;
    const auto json =
        R"({"version":1,"resources":[{"id":"sound/hit","kind":"sound","path":"hit.sound.json"},{"id":"audio/hit","kind":"audio-source","path":"hit.wav"}]})";
    REQUIRE(catalog.Read(json, d) == Status::Ok);
    REQUIRE(catalog.Entries()[0].Id.View() == "audio/hit");
    Bytes canonical;
    REQUIRE(catalog.Write(canonical) == Status::Ok);
    Catalog reopened;
    REQUIRE(reopened.Read(canonical.String(), d) == Status::Ok);
    Bytes second;
    REQUIRE(reopened.Write(second) == Status::Ok);
    REQUIRE(canonical.String() == second.String());
    for (const auto* invalid :
         {R"({"version":1,"version":1,"resources":[]})",
          R"({"version":2,"resources":[]})",
          R"({"version":1,"resources":[{"id":"audio/x","kind":"audio-source","path":"../x.wav"}]})"})
    {
        REQUIRE(catalog.Read(invalid, d) != Status::Ok);
        REQUIRE(catalog.Find("sound/hit") != nullptr);
    }
}
TEST_CASE("SHA256 matches published empty and abc vectors")
{
    const Digest empty = Hash({});
    REQUIRE(empty.Data[0] == 0xe3);
    REQUIRE(empty.Data[31] == 0x55);
    const uint8 abc[] = {'a', 'b', 'c'};
    const auto digest = Hash(abc);
    const uint8 expected[] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                              0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                              0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    for (usize i = 0; i < 32; ++i)
    {
        REQUIRE(digest.Data[i] == expected[i]);
    }
}
TEST_CASE("Root bounded saves preserve conflicts and reject symlink traversal")
{
    const auto root = std::filesystem::temp_directory_path() / std::string("ludus-content-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    const auto path = root.string();
    const uint8 a[] = {'a'}, b[] = {'b'};
    REQUIRE(SaveFile(path, "document.json", a, nullptr) == Status::Ok);
    Bytes data;
    REQUIRE(ReadFile(path, "document.json", 10, data) == Status::Ok);
    const auto digest = Hash(data.Data());
    REQUIRE(SaveFile(path, "document.json", b, &digest) == Status::Ok);
    REQUIRE(SaveFile(path, "document.json", a, &digest) == Status::Conflict);
    REQUIRE(ReadFile(path, "../secret", 10, data) == Status::Invalid);
    std::filesystem::create_directory_symlink("/tmp", root / "escape");
    REQUIRE(ReadFile(path, "escape/x", 10, data) != Status::Ok);
    std::filesystem::remove_all(root);
}
TEST_CASE("Descriptor-bound revisions survive atomic replacement and detect in-place changes")
{
    char directory[] = "/tmp/ludus-file-reader-XXXXXX";
    REQUIRE(mkdtemp(directory) != nullptr);
    const uint8 first[] = {'a', 'b', 'c'}, second[] = {'d', 'e'};
    REQUIRE(SaveFile(directory, "source.wav", first, nullptr) == Status::Ok);
    FileReader reader, clone;
    REQUIRE(reader.Open(directory, "source.wav") == Status::Ok);
    REQUIRE(reader.Clone(clone) == Status::Ok);
    const auto digest = Hash(first);
    REQUIRE(SaveFile(directory, "source.wav", second, &digest) == Status::Ok);
    uint8 bytes[3];
    usize count = 0;
    REQUIRE(reader.Read(bytes, count) == Status::Ok);
    REQUIRE(count == 3);
    REQUIRE(bytes[0] == 'a');
    REQUIRE(clone.Read(bytes, count) == Status::Ok);
    REQUIRE(bytes[0] == 'a');
    FileReader current;
    REQUIRE(current.Open(directory, "source.wav") == Status::Ok);
    REQUIRE(current.Size() == 2);
    REQUIRE(!ValidPath(std::string_view("bad\xff", 4)));
    REQUIRE(!ValidPath(std::string_view("bad\xc0\x80", 5)));
    REQUIRE(ValidPath("audio/音.wav"));
    ResourceId corrupt;
    corrupt.Length = 129;
    REQUIRE(corrupt.View().empty());
    Hasher partial;
    REQUIRE(partial.Add({first, 1}));
    REQUIRE(partial.Add({first + 1, 2}));
    REQUIRE(partial.Finish() == Hash(first));
    std::filesystem::remove_all(directory);
}
