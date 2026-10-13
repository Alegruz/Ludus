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

namespace
{
/// The `level` member of an Editor response, as canonical bytes.
std::string GetResponseMember(const Array<uint8>& response, std::string_view member)
{
    detail::JsonTree tree;
    detail::JsonIndex root = detail::NO_JSON_NODE;
    content::MessageText message;
    REQUIRE(detail::ParsePythonJson(ToSpan(response), tree, root, message));
    const detail::JsonIndex value = tree.GetMember(root, member);
    REQUIRE(value != detail::NO_JSON_NODE);
    if (tree.GetKind(value) == detail::JsonKind::String)
    {
        return std::string(tree.GetText(value));
    }
    Array<uint8> bytes;
    REQUIRE(detail::WriteCanonicalJson(tree, value, bytes));
    return ToText(bytes);
}

/// Compare one Editor response and its scene with the scene_editor.py capture.
void CheckEditorStep(const EditorSceneResult& result, const std::string& step)
{
    INFO(step);
    const std::filesystem::path editor = std::filesystem::path(GOLDENS) / "expected/files/editor";
    CHECK(ToText(result.Response) == ReadWhole(editor / (step + ".json")) + "\n");
    const std::string scenes = ReadWhole(editor / "scenes.json");
    const usize at = scenes.find("\"" + step + "\"");
    if (at == std::string::npos)
    {
        CHECK(result.Scene.GetSize() == 0);
        return;
    }
    CHECK(scenes.find(HexOf(ToText(result.Scene)), at) == scenes.find("\"sha256\": \"", at) + 11);
}
} // namespace

TEST_CASE("In-process Editor operations match scene_editor.py responses", "[scene_assets][goldens]")
{
    const std::filesystem::path work = MakeWorkDirectory();
    const std::filesystem::path project = work / "project";
    std::filesystem::create_directories(project);
    const std::string root = project.string();
    const std::string source = std::string(GOLDENS) + "/source";
    SequentialUuids counter;
    UuidSource uuids{&SequentialUuids::Generate, &counter};
    AssetOptions options;
    options.UuidsOrNull = &uuids;
    options.SourceOrigin = VIRTUAL_ORIGIN;
    AssetError error;
    Array<uint8> level;
    REQUIRE(CreateLevel("Golden", options, level, error));

    usize stages = 0;
    EditorSceneProgress progress{[](void* context, std::string_view) noexcept { ++*static_cast<usize*>(context); },
                                 &stages};
    EditorSceneRequest request;
    request.Root = root;
    request.Source = source;
    request.ProgressOrNull = &progress;

    request.Operation = EditorSceneOperation::Import;
    request.Level = ToSpan(level);
    EditorSceneResult imported;
    INFO(Describe(error));
    REQUIRE(OperateEditorScene(request, options, imported, error));
    CheckEditorStep(imported, "1-import");
    CHECK(ToText(imported.Catalog) ==
          ReadWhole(std::filesystem::path(GOLDENS) / "expected/files/editor/1-import.catalog.json"));
    CHECK(stages > 0);
    CHECK(std::filesystem::exists(project / "assets/catalog.json") == false);

    const std::string candidate = GetResponseMember(imported.Response, "level");
    request.Operation = EditorSceneOperation::CommitImport;
    request.Level = ToSpan(candidate);
    request.Catalog = ToSpan(imported.Catalog);
    request.HasExpected = false;
    EditorSceneResult committed;
    REQUIRE(OperateEditorScene(request, options, committed, error));
    CheckEditorStep(committed, "2-commit");

    request.Operation = EditorSceneOperation::Save;
    request.Path = "scenes/main.lulevel";
    request.Catalog = {};
    EditorSceneResult saved;
    REQUIRE(OperateEditorScene(request, options, saved, error));
    CheckEditorStep(saved, "3-save");

    request.Operation = EditorSceneOperation::Open;
    request.Level = {};
    EditorSceneResult opened;
    REQUIRE(OperateEditorScene(request, options, opened, error));
    CheckEditorStep(opened, "4-open");

    std::string renamed = GetResponseMember(opened.Response, "level");
    const usize name = renamed.find("\"name\":", renamed.find("\"objects\":"));
    REQUIRE(name != std::string::npos);
    renamed.replace(name + 8, renamed.find('"', name + 8) - (name + 8), "Renamed");
    request.Operation = EditorSceneOperation::Assemble;
    request.Level = ToSpan(renamed);
    EditorSceneResult assembled;
    REQUIRE(OperateEditorScene(request, options, assembled, error));
    CheckEditorStep(assembled, "5-assemble");

    const std::string disk = GetResponseMember(opened.Response, "disk");
    request.Operation = EditorSceneOperation::Save;
    request.HasExpected = true;
    request.Expected = disk;
    EditorSceneResult resaved;
    REQUIRE(OperateEditorScene(request, options, resaved, error));
    CheckEditorStep(resaved, "6-save");

    // The bytes on disk changed: the stale digest now conflicts.
    REQUIRE_FALSE(OperateEditorScene(request, options, resaved, error));
    CHECK(error.Code == AssetErrorCode::Conflict);

    bool isCancelled = true;
    Cancellation cancel{[](void* context) noexcept { return *static_cast<bool*>(context); }, &isCancelled};
    AssetOptions cancelling = options;
    cancelling.CancelOrNull = &cancel;
    request.Operation = EditorSceneOperation::Import;
    request.Level = ToSpan(renamed);
    request.HasExpected = false;
    REQUIRE_FALSE(OperateEditorScene(request, cancelling, imported, error));
    CHECK(error.Code == AssetErrorCode::InstallCancelled);

    std::error_code ignored;
    std::filesystem::remove_all(work, ignored);
}

namespace
{
/// Write a small general pbrt-v4 scene (outside the reference profile) to directory/name.
void WriteBestEffortSource(const std::filesystem::path& directory, std::string_view name)
{
    std::filesystem::create_directories(directory);
    std::ofstream(directory / std::string(name), std::ios::binary)
        << "LookAt 0 1 4  0 0 0  0 1 0\nCamera \"perspective\" \"float fov\" [ 45 ]\n"
           "Film \"rgb\" \"integer xresolution\" [ 64 ] \"integer yresolution\" [ 64 ]\n"
           "Sampler \"halton\"\nWorldBegin\n"
           "LightSource \"infinite\" \"rgb L\" [ .2 .2 .2 ]\n"
           "Material \"diffuse\" \"rgb reflectance\" [ .6 .5 .4 ]\n"
           "Shape \"trianglemesh\" \"point3 P\" [ -1 0 -1  1 0 -1  1 0 1  -1 0 1 ]\n"
           "  \"integer indices\" [ 0 1 2  0 2 3 ]\n"
           "AttributeBegin\nAreaLightSource \"diffuse\" \"rgb L\" [ 4 4 4 ]\n"
           "Shape \"sphere\" \"float radius\" [ .25 ]\nAttributeEnd\n";
    std::ofstream(directory / "LICENSE.txt", std::ios::binary) << "CC0 synthetic best-effort fixture\n";
}
} // namespace

TEST_CASE("Editor import routes general pbrt files to pbrt-best-effort-v1", "[scene_assets]")
{
    const std::filesystem::path work = MakeWorkDirectory();
    const std::filesystem::path project = work / "project";
    const std::filesystem::path source = work / "source";
    std::filesystem::create_directories(project);
    WriteBestEffortSource(source, "room.pbrt");
    const std::string root = project.string();
    const std::string sourceText = source.string();
    SequentialUuids counter;
    UuidSource uuids{&SequentialUuids::Generate, &counter};
    AssetOptions options;
    options.UuidsOrNull = &uuids;
    options.SourceOrigin = VIRTUAL_ORIGIN;
    options.SourceEntry = "room.pbrt";
    AssetError error;
    Array<uint8> level;
    REQUIRE(CreateLevel("Best effort", options, level, error));
    EditorSceneRequest request;
    request.Root = root;
    request.Source = sourceText;
    request.Level = ToSpan(level);

    // The dry-run preview writes nothing and describes the planned cook.
    request.Operation = EditorSceneOperation::PreviewImport;
    EditorSceneResult preview;
    INFO(Describe(error));
    REQUIRE(OperateEditorScene(request, options, preview, error));
    const std::string previewText = ToText(preview.Response);
    CHECK(GetResponseMember(preview.Response, "profile") == "pbrt-best-effort-v1");
    CHECK(previewText.find("\"dry_run\":true") != std::string::npos);
    CHECK(previewText.find("ENVIRONMENT_FALLBACK") != std::string::npos);
    CHECK(preview.Scene.IsEmpty() == true);
    CHECK(std::filesystem::exists(project / "assets") == false);

    // Import, commit and reassemble: the stored report comes back with the level.
    request.Operation = EditorSceneOperation::Import;
    EditorSceneResult imported;
    const bool isImported = OperateEditorScene(request, options, imported, error);
    INFO(Describe(error));
    REQUIRE(isImported);
    CHECK(imported.Scene.IsEmpty() == false);
    const std::string candidate = GetResponseMember(imported.Response, "level");
    request.Operation = EditorSceneOperation::CommitImport;
    request.Level = ToSpan(candidate);
    request.Catalog = ToSpan(imported.Catalog);
    EditorSceneResult committed;
    REQUIRE(OperateEditorScene(request, options, committed, error));
    request.Operation = EditorSceneOperation::Assemble;
    request.Catalog = {};
    EditorSceneResult assembled;
    REQUIRE(OperateEditorScene(request, options, assembled, error));
    const std::string reports = GetResponseMember(assembled.Response, "import_reports");
    CHECK(reports.find("\"importer\":\"pbrt-best-effort-v1\"") != std::string::npos);
    CHECK(reports.find("\"dry_run\":false") != std::string::npos);
    CHECK(GetResponseMember(assembled.Response, "warnings").find("ENVIRONMENT_FALLBACK") != std::string::npos);

    // A changed source input is detected before publication.
    std::ofstream(source / "room.pbrt", std::ios::app) << "# edited\n";
    request.Operation = EditorSceneOperation::CommitImport;
    request.Catalog = ToSpan(imported.Catalog);
    EditorSceneResult stale;
    CHECK(OperateEditorScene(request, options, stale, error) == false);

    std::error_code ignored;
    std::filesystem::remove_all(work, ignored);
}

TEST_CASE("A confirmed import must match the approved preview", "[scene_assets]")
{
    const std::filesystem::path work = MakeWorkDirectory();
    const std::filesystem::path project = work / "project";
    const std::filesystem::path source = work / "source";
    std::filesystem::create_directories(project);
    WriteBestEffortSource(source, "room.pbrt");
    const std::string root = project.string();
    const std::string sourceText = source.string();
    AssetOptions options;
    options.SourceOrigin = VIRTUAL_ORIGIN;
    options.SourceEntry = "room.pbrt";
    AssetError error;
    Array<uint8> level;
    REQUIRE(CreateLevel("Preview token", options, level, error));
    EditorSceneRequest request;
    request.Root = root;
    request.Source = sourceText;
    request.Level = ToSpan(level);
    request.Operation = EditorSceneOperation::PreviewImport;
    EditorSceneResult preview;
    INFO(Describe(error));
    REQUIRE(OperateEditorScene(request, options, preview, error));
    const std::string digest = GetResponseMember(preview.Response, "source_digest");
    REQUIRE(digest.size() == 64); // hexadecimal SHA-256

    request.Operation = EditorSceneOperation::Import;
    options.ExpectedSourceDigest = digest;
    EditorSceneResult matching;
    CHECK(OperateEditorScene(request, options, matching, error) == true);

    std::ofstream(source / "room.pbrt", std::ios::app) << "# edited after the preview\n";
    EditorSceneResult stale;
    CHECK(OperateEditorScene(request, options, stale, error) == false);
    CHECK(error.Code == AssetErrorCode::Conflict);

    std::error_code ignored;
    std::filesystem::remove_all(work, ignored);
}

TEST_CASE("Two pbrt entries in one directory keep separate origins", "[scene_assets]")
{
    const std::filesystem::path work = MakeWorkDirectory();
    const std::filesystem::path project = work / "project";
    const std::filesystem::path source = work / "source";
    std::filesystem::create_directories(project);
    WriteBestEffortSource(source, "room.pbrt");
    WriteBestEffortSource(source, "hall.pbrt");
    const std::string root = project.string();
    const std::string sourceText = source.string();
    SequentialUuids counter;
    UuidSource uuids{&SequentialUuids::Generate, &counter};
    AssetOptions options;
    options.UuidsOrNull = &uuids;
    AssetError error;
    Array<uint8> created;
    REQUIRE(CreateLevel("Two entries", options, created, error));
    std::string level(reinterpret_cast<const char*>(created.GetData()), created.GetSize());
    EditorSceneRequest request;
    request.Root = root;
    request.Source = sourceText;
    for (const std::string_view entry : {std::string_view("room.pbrt"), std::string_view("hall.pbrt")})
    {
        options.SourceEntry = entry;
        request.Operation = EditorSceneOperation::Import;
        request.Level = ToSpan(level);
        request.Catalog = {};
        EditorSceneResult imported;
        INFO(Describe(error));
        REQUIRE(OperateEditorScene(request, options, imported, error));
        level = GetResponseMember(imported.Response, "level");
        const std::string expected = GetResponseMember(imported.Response, "catalog_expected");
        request.Operation = EditorSceneOperation::CommitImport;
        request.Level = ToSpan(level);
        request.Catalog = ToSpan(imported.Catalog);
        request.HasExpected = expected.size() == 64;
        request.Expected = expected;
        EditorSceneResult committed;
        const bool isCommitted = OperateEditorScene(request, options, committed, error);
        INFO(Describe(error));
        REQUIRE(isCommitted);
        request.HasExpected = false;
    }
    // Each entry got its own logical scene rather than replacing the first.
    const usize importsAt = level.find("\"imports\":[");
    REQUIRE(importsAt != std::string::npos);
    const std::string imports = level.substr(importsAt, level.find(']', importsAt) - importsAt);
    CHECK(imports.find(',') != std::string::npos);

    std::error_code ignored;
    std::filesystem::remove_all(work, ignored);
}

TEST_CASE("scene-v4.pbrt keeps pbrt-reference-v1 and falls back only on profile rejection", "[scene_assets]")
{
    const std::filesystem::path work = MakeWorkDirectory();
    const std::filesystem::path project = work / "project";
    std::filesystem::create_directories(project);
    const std::string root = project.string();
    AssetOptions options;
    options.SourceEntry = "scene-v4.pbrt";
    AssetError error;
    EditorSceneRequest request;
    request.Root = root;
    request.Operation = EditorSceneOperation::PreviewImport;

    const std::string reference = std::string(GOLDENS) + "/source";
    request.Source = reference;
    EditorSceneResult kept;
    INFO(Describe(error));
    REQUIRE(OperateEditorScene(request, options, kept, error));
    CHECK(GetResponseMember(kept.Response, "profile") == "pbrt-reference-v1");
    CHECK(ToText(kept.Response).find("reference_rejection") == std::string::npos);

    const std::filesystem::path general = work / "general";
    WriteBestEffortSource(general, "scene-v4.pbrt");
    const std::string generalText = general.string();
    request.Source = generalText;
    EditorSceneResult fallback;
    REQUIRE(OperateEditorScene(request, options, fallback, error));
    CHECK(GetResponseMember(fallback.Response, "profile") == "pbrt-best-effort-v1");
    CHECK(ToText(fallback.Response).find("\"reference_rejection\":") != std::string::npos);

    // An empty entry is the unchanged reference behaviour: no fallback.
    options.SourceEntry = {};
    EditorSceneResult rejected;
    CHECK(OperateEditorScene(request, options, rejected, error) == false);
    CHECK(error.Code == AssetErrorCode::UnsupportedScene);

    std::error_code ignored;
    std::filesystem::remove_all(work, ignored);
}
