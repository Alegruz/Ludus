// Golden and behaviour tests for the native scene asset catalog (scene_assets.py port).

#include <ludus/content/content.h>
#include <ludus/content/scene_cook/scene_assets.h>
#include <ludus/content/scene_cook/scene_cook.h>

#include "python_json.h"
#include "uuid.h"

#include <ludus/foundation/parsing/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>

namespace
{
using namespace ludus::foundation;
namespace content = ludus::content;
namespace detail = ludus::content::scene_cook::detail;
using namespace ludus::content::scene_cook;

constexpr std::string_view GOLDENS = LUDUS_SCENE_GOLDENS_DIR;
constexpr std::string_view VIRTUAL_ORIGIN = "/ludus-golden/source";

std::string ToText(const Array<uint8>& bytes)
{
    return {reinterpret_cast<const char*>(bytes.GetData()), bytes.GetSize()};
}

std::span<const uint8> ToSpan(const Array<uint8>& bytes)
{
    return {bytes.GetData(), bytes.GetSize()};
}

std::span<const uint8> ToSpan(std::string_view text)
{
    return {reinterpret_cast<const uint8*>(text.data()), text.size()};
}

std::string HexOf(std::string_view data)
{
    static constexpr char HEX[] = "0123456789abcdef";
    const content::Digest digest = content::Hash(ToSpan(data));
    std::string text;
    for (const uint8 value : digest.Data)
    {
        text += HEX[value >> 4];
        text += HEX[value & 0xf];
    }
    return text;
}

std::string ReadWhole(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

struct GoldenRecord
{
    std::string Sha256;
    uint64 Bytes = 0;
    bool IsInline = false;
};

/// expected/goldens.json "outputs".
std::map<std::string, GoldenRecord> ReadGoldenOutputs()
{
    const std::string text = ReadWhole(std::filesystem::path(GOLDENS) / "expected/goldens.json");
    parsing::JsonDocument document;
    parsing::ParseError error{};
    parsing::JsonLimits limits;
    limits.MaxObjectMembers = parsing::MAX_JSON_OBJECT_MEMBERS;
    limits.MaxInputBytes = usize{16} << 20;
    limits.MaxDepth = 64;
    REQUIRE(document.Read(text, error, limits) == parsing::ParseStatus::Ok);
    const parsing::JsonValue outputs = document.Root().Get("outputs");
    std::map<std::string, GoldenRecord> records;
    for (usize i = 0; i < outputs.MemberCount(); ++i)
    {
        std::string_view name;
        parsing::JsonValue value;
        REQUIRE(outputs.MemberAt(i, name, value));
        GoldenRecord record;
        std::string_view sha;
        REQUIRE(value.Get("sha256").String(sha));
        REQUIRE(value.Get("bytes").Integer(record.Bytes));
        REQUIRE(value.Get("inline").Boolean(record.IsInline));
        record.Sha256 = std::string(sha);
        records.emplace(std::string(name), record);
    }
    return records;
}

/// Compare one produced file with its golden record and, when inline, its stored bytes.
void CheckGolden(const std::map<std::string, GoldenRecord>& goldens, const std::string& name, const std::string& data)
{
    INFO(name);
    const std::map<std::string, GoldenRecord>::const_iterator found = goldens.find(name);
    REQUIRE(found != goldens.end());
    CHECK(data.size() == found->second.Bytes);
    CHECK(HexOf(data) == found->second.Sha256);
    if (found->second.IsInline == true)
    {
        CHECK(data == ReadWhole(std::filesystem::path(GOLDENS) / "expected/files" / name));
    }
}

/// Every regular file below root, as capture.py's tree() listed them.
std::map<std::string, std::string> ReadTree(const std::filesystem::path& root)
{
    std::map<std::string, std::string> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (entry.is_regular_file())
        {
            files.emplace(entry.path().lexically_relative(root).generic_string(), ReadWhole(entry.path()));
        }
    }
    return files;
}

/// uuid.UUID(int=n) for n = 1, 2, ... in call order.
struct SequentialUuids
{
    uint64 Next = 0;
    static void Generate(void* context, uint8 (&outBytes)[16]) noexcept
    {
        SequentialUuids* self = static_cast<SequentialUuids*>(context);
        const uint64 value = ++self->Next;
        for (usize i = 0; i < 16; ++i)
        {
            outBytes[i] = i < 8 ? 0 : static_cast<uint8>(value >> ((15 - i) * 8));
        }
    }
};

std::filesystem::path MakeWorkDirectory()
{
    char directory[] = "/tmp/ludus-scene-assets-XXXXXX";
    REQUIRE(mkdtemp(directory) != nullptr);
    return directory;
}

std::string Describe(const AssetError& error)
{
    return std::string(GetAssetErrorCodeName(error.Code)) + ": " + std::string(error.Message.View());
}

/// A project with one imported and saved level, using the golden normalisation.
struct GoldenProject
{
    std::filesystem::path Work = MakeWorkDirectory();
    std::filesystem::path Project = Work / "project";
    std::string Source = std::string(GOLDENS) + "/source";
    SequentialUuids Counter;
    UuidSource Uuids{&SequentialUuids::Generate, &Counter};
    AssetOptions Options;
    Array<uint8> Level;
    Array<uint8> Saved;

    GoldenProject()
    {
        Options.UuidsOrNull = &Uuids;
        Options.SourceOrigin = VIRTUAL_ORIGIN;
        std::filesystem::create_directories(Project);
        AssetError error;
        Array<uint8> empty;
        REQUIRE(CreateLevel("Golden", Options, empty, error));
        INFO(Describe(error));
        REQUIRE(ImportScene(Project.string(), Source, ToSpan(empty), Options, Level, error));
        REQUIRE(SaveLevel(Project.string(), "scenes/main.lulevel", ToSpan(Level), nullptr, Saved, error));
    }

    ~GoldenProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(Work, ignored);
    }
};
} // namespace

TEST_CASE("SHA-1 and uuid5 match hashlib and the uuid module", "[scene_assets]")
{
    const auto sha1 = [](std::string_view text) {
        uint8 digest[20]{};
        detail::ComputeSha1(ToSpan(text), digest);
        static constexpr char HEX[] = "0123456789abcdef";
        std::string hex;
        for (const uint8 value : digest)
        {
            hex += HEX[value >> 4];
            hex += HEX[value & 0xf];
        }
        return hex;
    };
    CHECK(sha1("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(sha1(std::string(55, 'a')) == "c1c8bbdc22796e28c0e15163d20899b65621d65a");
    CHECK(sha1(std::string(56, 'a')) == "c2db330f6083854c99d4b5bfb6e8f29f201be699");
    CHECK(sha1(std::string(1000, 'a')) == "291e9a6c66994949b57ba5e650361e98fc36b1ba");

    detail::Uuid space;
    space.Bytes[15] = 2;
    detail::Uuid named;
    REQUIRE(detail::MakeNameUuid(space, "mesh/0", named));
    Array<uint8> text;
    detail::ByteWriter writer(text);
    detail::AppendUuid(writer, named);
    CHECK(ToText(text) == "312d1234-1ad2-5894-9ef8-dc2bfad15547");
}

TEST_CASE("uuid.UUID() string forms are accepted as Python accepts them", "[scene_assets]")
{
    detail::Uuid value;
    CHECK(detail::ParseUuid("00000000-0000-0000-0000-00000000000a", value));
    CHECK(value.Bytes[15] == 0x0a);
    CHECK(detail::ParseUuid("urn:uuid:{00000000-0000-0000-0000-00000000000A}", value));
    CHECK(detail::ParseUuid("0000000000000000000000000000000a", value));
    CHECK_FALSE(detail::ParseUuid("00000000-0000-0000-0000-00000000000", value));
    CHECK_FALSE(detail::ParseUuid("g0000000-0000-0000-0000-00000000000a", value));
    CHECK_FALSE(detail::ParseUuid("", value));
}

TEST_CASE("Python json round trip keeps ints, floats, order and escapes", "[scene_assets]")
{
    detail::JsonTree tree;
    detail::JsonIndex root = detail::NO_JSON_NODE;
    content::MessageText message;
    const std::string_view input = R"({"b":[1,1.0,-0.0,2.5e-3,"\ud800x\u00e9",true,null],"a":{}, "c":"é"})";
    REQUIRE(detail::ParsePythonJson(ToSpan(input), tree, root, message));
    Array<uint8> output;
    REQUIRE(detail::WriteCanonicalJson(tree, root, output));
    CHECK(ToText(output) == "{\"a\":{},\"b\":[1,1.0,-0.0,0.0025,\"\\ud800x\\u00e9\",true,null],\"c\":\"\\u00e9\"}\n");
    CHECK(tree.GetKey(root, 0) == "b");

    detail::JsonTree duplicate;
    CHECK_FALSE(detail::ParsePythonJson(ToSpan(std::string_view(R"({"a":1,"a":2})")), duplicate, root, message));
    CHECK(message.View() == "Duplicate JSON field: a");

    detail::JsonTree special;
    REQUIRE(detail::ParsePythonJson(ToSpan(std::string_view("[NaN, Infinity]")), special, root, message));
    CHECK_FALSE(detail::WriteCanonicalJson(special, root, output));
}

TEST_CASE("New levels match the reference document", "[scene_assets]")
{
    SequentialUuids counter;
    UuidSource uuids{&SequentialUuids::Generate, &counter};
    AssetOptions options;
    options.UuidsOrNull = &uuids;
    Array<uint8> level;
    AssetError error;
    REQUIRE(CreateLevel("Golden", options, level, error));
    CHECK(ToText(level) ==
          "{\"camera\":{\"eye\":[0,0,3],\"fov\":1.0471975511965976,\"target\":[0,0,0],\"up\":[0,1,0]},"
          "\"id\":\"00000000-0000-0000-0000-000000000001\",\"imports\":[],\"name\":\"Golden\",\"objects\":[],"
          "\"profile\":\"resident-reference-v1\",\"version\":1}\n");
    CHECK(ValidateLevel(ToSpan(level), error));
}

TEST_CASE("Scene asset pipeline is byte-identical to the Python goldens", "[scene_assets][goldens]")
{
    const std::map<std::string, GoldenRecord> goldens = ReadGoldenOutputs();
    GoldenProject golden;
    const std::string root = golden.Project.string();
    AssetError error;

    Array<uint8> reopened;
    REQUIRE(OpenLevel(root, "scenes/main.lulevel", reopened, error));
    CHECK(ToText(reopened) == ToText(golden.Level));

    // A second import of the same origin must reuse logical identities.
    Array<uint8> second;
    Array<uint8> secondImported;
    Array<uint8> secondSaved;
    REQUIRE(CreateLevel("Second", golden.Options, second, error));
    REQUIRE(ImportScene(root, golden.Source, ToSpan(second), golden.Options, secondImported, error));
    REQUIRE(SaveLevel(root, "scenes/second.lulevel", ToSpan(secondImported), nullptr, secondSaved, error));

    Array<uint8> bundle;
    Array<uint8> dump;
    Array<uint8> published;
    REQUIRE(AssembleLevel(root, ToSpan(golden.Level), {}, bundle, error));
    REQUIRE(DumpLevel(root, ToSpan(golden.Level), {}, DumpLayout::Canonical, dump, error));
    const std::filesystem::path release = golden.Work / "release";
    INFO(Describe(error));
    REQUIRE(PublishLevel(root, ToSpan(golden.Level), release.string(), DumpLayout::Canonical, published, error));
    CHECK(ToText(published) == ToText(dump));

    CheckGolden(goldens, "saved/main.lulevel", ToText(golden.Saved));
    CheckGolden(goldens, "assemble/main.luscn", ToText(bundle));
    CheckGolden(goldens, "dump/main.json", ToText(dump));
    usize expectedProject = 0;
    usize expectedRelease = 0;
    for (const std::pair<const std::string, GoldenRecord>& item : goldens)
    {
        const std::string& name = item.first;
        expectedProject += name.starts_with("project/") ? 1 : 0;
        expectedRelease += name.starts_with("release/") ? 1 : 0;
    }
    const std::map<std::string, std::string> project = ReadTree(golden.Project);
    const std::map<std::string, std::string> releaseFiles = ReadTree(release);
    CHECK(project.size() == expectedProject);
    CHECK(releaseFiles.size() == expectedRelease);
    for (const std::pair<const std::string, std::string>& item : project)
    {
        CheckGolden(goldens, "project/" + item.first, item.second);
    }
    for (const std::pair<const std::string, std::string>& item : releaseFiles)
    {
        CheckGolden(goldens, "release/" + item.first, item.second);
    }
}

TEST_CASE("Saving refuses to overwrite changed or unexpected levels", "[scene_assets]")
{
    GoldenProject golden;
    AssetError error;
    Array<uint8> saved;
    CHECK_FALSE(SaveLevel(golden.Project.string(), "scenes/main.lulevel", ToSpan(golden.Level), nullptr, saved, error));
    CHECK(error.Code == AssetErrorCode::Conflict);
    CHECK(SaveLevel(golden.Project.string(), "scenes/main.lulevel", ToSpan(golden.Level), &golden.Saved, saved, error));
    CHECK_FALSE(SaveLevel(golden.Project.string(), "scenes/main.json", ToSpan(golden.Level), nullptr, saved, error));
    CHECK(error.Message.View() == "Levels use the .lulevel extension");
    CHECK_FALSE(SaveLevel(golden.Project.string(), "../escape.lulevel", ToSpan(golden.Level), nullptr, saved, error));
    CHECK(error.Message.View() == "Project asset path must be relative and contained");
}

TEST_CASE("Corrupt artifacts and existing release directories are rejected", "[scene_assets]")
{
    GoldenProject golden;
    AssetError error;
    Array<uint8> dump;
    const std::filesystem::path release = golden.Work / "release";
    std::filesystem::create_directories(release);
    CHECK_FALSE(PublishLevel(golden.Project.string(),
                             ToSpan(golden.Level),
                             release.string(),
                             DumpLayout::Canonical,
                             dump,
                             error));
    CHECK(error.Code == AssetErrorCode::DestinationExists);

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(golden.Project / "assets/objects"))
    {
        if (entry.file_size() < 100)
        {
            std::filesystem::permissions(entry.path(),
                                         std::filesystem::perms::owner_write,
                                         std::filesystem::perm_options::add);
            std::ofstream(entry.path(), std::ios::binary | std::ios::app) << "x";
            break;
        }
    }
    Array<uint8> level;
    CHECK_FALSE(OpenLevel(golden.Project.string(), "scenes/main.lulevel", level, error));
    CHECK(error.Code == AssetErrorCode::AssetCorrupt);
}

TEST_CASE("Saved-level import publishes the catalog and level together", "[scene_assets]")
{
    GoldenProject golden;
    AssetError error;
    Array<uint8> imported;
    // Reimporting the same origin reuses identities; the new objects receive fresh UUIDs.
    REQUIRE(ImportSavedLevel(golden.Project.string(),
                             "scenes/main.lulevel",
                             golden.Source,
                             golden.Options,
                             imported,
                             error));
    Array<uint8> reopened;
    REQUIRE(OpenLevel(golden.Project.string(), "scenes/main.lulevel", reopened, error));
    CHECK(ToText(reopened) == ToText(imported));
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(golden.Project / "scenes"))
    {
        CHECK(entry.path().filename().string().starts_with(".scene-import-") == false);
    }
}

TEST_CASE("Import cancellation leaves the published catalog untouched", "[scene_assets]")
{
    GoldenProject golden;
    const std::string before = ReadWhole(golden.Project / "assets/catalog.json");
    Cancellation cancellation;
    cancellation.Requested = [](void*) noexcept { return true; };
    AssetOptions options = golden.Options;
    options.CancelOrNull = &cancellation;
    AssetError error;
    Array<uint8> level;
    CHECK_FALSE(ImportScene(golden.Project.string(), golden.Source, ToSpan(golden.Level), options, level, error));
    CHECK(error.Code == AssetErrorCode::InstallCancelled);
    CHECK(ReadWhole(golden.Project / "assets/catalog.json") == before);
}

TEST_CASE("Prepared candidates are rejected after a concurrent catalog change", "[scene_assets]")
{
    GoldenProject golden;
    AssetError error;
    Array<uint8> empty;
    REQUIRE(CreateLevel("Candidate", golden.Options, empty, error));
    ImportCandidate candidate;
    REQUIRE(PrepareImport(golden.Project.string(), golden.Source, ToSpan(empty), golden.Options, candidate, error));
    CHECK(candidate.HasPublishedCatalog);
    std::ofstream(golden.Project / "assets/catalog.json", std::ios::binary | std::ios::app) << " ";
    CHECK_FALSE(CommitImport(golden.Project.string(),
                             ToSpan(candidate.Level),
                             ToSpan(candidate.Catalog),
                             &candidate.PublishedCatalog,
                             golden.Source,
                             golden.Options,
                             error));
    CHECK(error.Code == AssetErrorCode::Conflict);
}

TEST_CASE("Invalid levels report the reference messages", "[scene_assets]")
{
    AssetError error;
    CHECK_FALSE(ValidateLevel(ToSpan(std::string_view("{}")), error));
    CHECK(error.Message.View() == "Invalid level fields");
    const std::string_view degenerate =
        R"({"camera":{"eye":[0,0,0],"fov":1.0,"target":[0,0,0],"up":[0,1,0]},)"
        R"("id":"00000000-0000-0000-0000-000000000001","imports":[],"name":"x","objects":[],)"
        R"("profile":"resident-reference-v1","version":1})";
    CHECK_FALSE(ValidateLevel(ToSpan(degenerate), error));
    CHECK(error.Message.View() == "Degenerate authored camera");
}
