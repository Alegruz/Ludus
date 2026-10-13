// ludus-cook: native replacement for the retired Python scene cooker and the
// `ludus scene` CLI commands.
//
//   ludus-cook cook SOURCE OUTPUT
//   ludus-cook pbrt ENTRY.pbrt OUTPUT     (best-effort pbrt-v4 import)
//   ludus-cook pbrt ENTRY.pbrt --dry-run
//   ludus-cook scene new      PROJECT LEVEL [--name NAME] [--json]
//   ludus-cook scene open     PROJECT LEVEL [--json]
//   ludus-cook scene import   PROJECT LEVEL SOURCE [--json]
//   ludus-cook scene dump     PROJECT LEVEL [--json]
//   ludus-cook scene assemble PROJECT LEVEL OUTPUT [--json]
//   ludus-cook scene publish  PROJECT LEVEL OUTPUT [--json]
//
// `cook` writes OUTPUT/scene.luscn, OUTPUT/manifest.json and OUTPUT/LICENSE.txt
// and prints the manifest. `pbrt` writes OUTPUT/scene.luscn,
// OUTPUT/import-report.json and OUTPUT/LICENSE.txt and prints the report;
// --dry-run only parses and plans and prints the report. Every `scene` command prints the level's dump (the
// level, its exact resident closure and byte total); --json prints it on one
// line with "schema": 1, or the error as {"schema": 1, "error": {...}}.
// Exit status: 0 success, 1 failure, 2 usage, 3 missing host facilities,
// 130 cancelled.

#include <ludus/content/scene_cook/best_effort_import.h>
#include <ludus/content/scene_cook/scene_assets.h>
#include <ludus/content/scene_cook/scene_cook.h>

#include <string_view>
#include <unistd.h>

namespace
{
using ludus::foundation::Array;
using ludus::foundation::core::uint8;
using ludus::foundation::core::usize;
using namespace ludus::content::scene_cook;

constexpr int EXIT_OK = 0;
constexpr int EXIT_FAILED = 1;
constexpr int EXIT_USAGE = 2;
constexpr int EXIT_UNAVAILABLE = 3;
constexpr int EXIT_CANCELLED = 130;

void WriteAll(int descriptor, std::string_view text) noexcept
{
    while (text.empty() == false)
    {
        const ssize_t written = write(descriptor, text.data(), text.size());
        if (written <= 0)
        {
            return;
        }
        text.remove_prefix(static_cast<usize>(written));
    }
}

[[nodiscard]] std::string_view GetView(const Array<uint8>& bytes) noexcept
{
    return std::string_view(reinterpret_cast<const char*>(bytes.GetData()), bytes.GetSize());
}

[[nodiscard]] std::span<const uint8> GetSpan(const Array<uint8>& bytes) noexcept
{
    return std::span<const uint8>(bytes.GetData(), bytes.GetSize());
}

int PrintUsage() noexcept
{
    WriteAll(2,
             "usage: ludus-cook cook SOURCE OUTPUT\n"
             "       ludus-cook pbrt ENTRY.pbrt {OUTPUT|--dry-run}\n"
             "       ludus-cook scene new PROJECT LEVEL [--name NAME] [--json]\n"
             "       ludus-cook scene {open|dump} PROJECT LEVEL [--json]\n"
             "       ludus-cook scene import PROJECT LEVEL SOURCE [--json]\n"
             "       ludus-cook scene {assemble|publish} PROJECT LEVEL OUTPUT [--json]\n");
    return EXIT_USAGE;
}

/// Write one JSON string literal (ASCII-escaped) for error reports.
void WriteJsonString(std::string_view text) noexcept
{
    static constexpr char HEX[] = "0123456789abcdef";
    WriteAll(1, "\"");
    for (const char item : text)
    {
        const uint8 value = static_cast<uint8>(item);
        if (item == '"' || item == '\\')
        {
            const char escaped[2] = {'\\', item};
            WriteAll(1, std::string_view(escaped, 2));
        }
        else if (value < 0x20)
        {
            const char escaped[6] = {'\\', 'u', '0', '0', HEX[value >> 4], HEX[value & 0xf]};
            WriteAll(1, std::string_view(escaped, 6));
        }
        else
        {
            WriteAll(1, std::string_view(&item, 1));
        }
    }
    WriteAll(1, "\"");
}

int ReportFailure(const AssetError& error, bool isJson) noexcept
{
    if (isJson == true)
    {
        WriteAll(1, "{\"schema\": 1, \"error\": {\"code\": ");
        WriteJsonString(GetAssetErrorCodeName(error.Code));
        WriteAll(1, ", \"message\": ");
        WriteJsonString(error.Message.View());
        WriteAll(1, "}}\n");
    }
    else
    {
        WriteAll(2, "error [");
        WriteAll(2, GetAssetErrorCodeName(error.Code));
        WriteAll(2, "]: ");
        WriteAll(2, error.Message.View());
        WriteAll(2, "\n");
    }
    if (error.Code == AssetErrorCode::InstallCancelled)
    {
        return EXIT_CANCELLED;
    }
    return error.Code == AssetErrorCode::MissingTools ? EXIT_UNAVAILABLE : EXIT_FAILED;
}

int RunCook(int argc, char** argv) noexcept
{
    if (argc != 4)
    {
        return PrintUsage();
    }
    CookResult result;
    CookError error;
    if (Cook(argv[2], result, error) == false || WriteCook(argv[3], result, error) == false)
    {
        WriteAll(2, "conversion failed: ");
        WriteAll(2, error.Message.View());
        WriteAll(2, "\n");
        return EXIT_FAILED;
    }
    WriteAll(1, GetView(result.Manifest));
    return EXIT_OK;
}

int RunPbrt(int argc, char** argv) noexcept
{
    if (argc != 4)
    {
        return PrintUsage();
    }
    BestEffortOptions options;
    options.IsDryRun = std::string_view(argv[3]) == "--dry-run";
    BestEffortResult result;
    CookError error;
    const bool isImported = ImportPbrtBestEffort(argv[2], options, result, error);
    WriteAll(1, GetView(result.Report));
    if (isImported == false || (options.IsDryRun == false && WriteBestEffortImport(argv[3], result, error) == false))
    {
        WriteAll(2, "import failed: ");
        WriteAll(2, error.Message.View());
        WriteAll(2, "\n");
        return error.Result == ludus::content::Status::Cancelled ? EXIT_CANCELLED : EXIT_FAILED;
    }
    return EXIT_OK;
}

/// Parsed `scene` command line.
struct SceneCommand
{
    std::string_view Action;
    std::string_view Project;
    std::string_view Level;
    std::string_view Operand;
    std::string_view Name = "Scene";
    bool IsJson = false;
};

[[nodiscard]] bool ParseScene(int argc, char** argv, SceneCommand& outCommand) noexcept
{
    std::string_view positional[4];
    usize count = 0;
    for (int i = 2; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        if (argument == "--json")
        {
            outCommand.IsJson = true;
        }
        else if (argument == "--name" && i + 1 < argc)
        {
            outCommand.Name = argv[++i];
        }
        else if (count < 4 && argument.starts_with("--") == false)
        {
            positional[count++] = argument;
        }
        else
        {
            return false;
        }
    }
    if (count < 3)
    {
        return false;
    }
    outCommand.Action = positional[0];
    outCommand.Project = positional[1];
    outCommand.Level = positional[2];
    outCommand.Operand = positional[3];
    const std::string_view action = outCommand.Action;
    const bool hasOperand = action == "import" || action == "assemble" || action == "publish";
    const bool isKnown = hasOperand == true || action == "new" || action == "open" || action == "dump";
    return isKnown == true && (count == 4) == hasOperand && (action == "new" || outCommand.Name == "Scene");
}

int RunScene(int argc, char** argv) noexcept
{
    SceneCommand command;
    if (ParseScene(argc, argv, command) == false)
    {
        return PrintUsage();
    }
    const AssetOptions options;
    AssetError error;
    Array<uint8> level;
    bool isOk = true;
    if (command.Action == "new")
    {
        Array<uint8> created;
        isOk = CreateLevel(command.Name, options, created, error) == true &&
               SaveLevel(command.Project, command.Level, GetSpan(created), nullptr, level, error) == true;
    }
    else
    {
        isOk = OpenLevel(command.Project, command.Level, level, error);
        if (isOk == true && command.Action == "import")
        {
            isOk = ImportSavedLevel(command.Project, command.Level, command.Operand, options, level, error);
        }
        else if (isOk == true && command.Action == "assemble")
        {
            Array<uint8> bundle;
            isOk = AssembleLevel(command.Project, GetSpan(level), {}, bundle, error) == true &&
                   WriteFileAtomically(command.Operand, GetSpan(bundle), error) == true;
        }
        else if (isOk == true && command.Action == "publish")
        {
            Array<uint8> published;
            isOk =
                PublishLevel(command.Project, GetSpan(level), command.Operand, DumpLayout::Canonical, published, error);
        }
    }
    Array<uint8> dump;
    const DumpLayout layout = command.IsJson == true ? DumpLayout::Line : DumpLayout::Indented;
    if (isOk == false || DumpLevel(command.Project, GetSpan(level), {}, layout, dump, error) == false)
    {
        return ReportFailure(error, command.IsJson);
    }
    if (command.IsJson == true)
    {
        // {"schema": 1, **payload}
        WriteAll(1, "{\"schema\": 1, ");
        WriteAll(1, GetView(dump).substr(1));
    }
    else
    {
        WriteAll(1, GetView(dump));
    }
    return EXIT_OK;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc >= 2 && std::string_view(argv[1]) == "cook")
    {
        return RunCook(argc, argv);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "pbrt")
    {
        return RunPbrt(argc, argv);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "scene")
    {
        return RunScene(argc, argv);
    }
    return PrintUsage();
}
