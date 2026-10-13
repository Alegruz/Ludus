// ludus-cook: native replacement for the retired Python scene cooker CLI.
//
//   ludus-cook cook SOURCE OUTPUT
//
// Cooks SOURCE (scene-v4.pbrt + LICENSE.txt, pbrt-reference-v1 profile) into
// OUTPUT/scene.luscn, OUTPUT/manifest.json and OUTPUT/LICENSE.txt, then prints
// the manifest. Exit status 0 on success, 1 on conversion failure, 2 on usage.

#include <ludus/content/scene_cook/scene_cook.h>

#include <string_view>
#include <unistd.h>

namespace
{
using ludus::foundation::core::usize;

void WriteAll(int descriptor, std::string_view text) noexcept
{
    while (!text.empty())
    {
        const ssize_t written = write(descriptor, text.data(), text.size());
        if (written <= 0)
        {
            return;
        }
        text.remove_prefix(static_cast<usize>(written));
    }
}

int PrintUsage() noexcept
{
    WriteAll(2, "usage: ludus-cook cook SOURCE OUTPUT\n");
    return 2;
}
} // namespace

int main(int argc, char** argv)
{
    using namespace ludus::content::scene_cook;
    if (argc != 4 || std::string_view(argv[1]) != "cook")
    {
        return PrintUsage();
    }
    CookResult result;
    CookError error;
    if (!Cook(argv[2], result, error) || !WriteCook(argv[3], result, error))
    {
        WriteAll(2, "conversion failed: ");
        WriteAll(2, error.Message.View());
        WriteAll(2, "\n");
        return 1;
    }
    WriteAll(1, std::string_view(reinterpret_cast<const char*>(result.Manifest.GetData()), result.Manifest.GetSize()));
    return 0;
}
