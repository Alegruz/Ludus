#include <ludus/foundation/filesystem/filesystem.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <unistd.h>

int ExerciseInstalledFilesystem() noexcept
{
    using namespace ludus::foundation;
    using namespace ludus::foundation::filesystem;
    char path[] = "/tmp/ludus-installed-fs-XXXXXX";
    if (::mkdtemp(path) == nullptr)
    {
        return 10;
    }
    struct Cleanup final
    {
        const char* Path;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }
    } cleanup{path};
    {
        std::ofstream file(std::filesystem::path(path) / "asset", std::ios::binary);
        file << "asset";
        file.close();
        if (!file.good())
        {
            return 10;
        }
    }
    Directory directory;
    File file, clone;
    uint8 bytes[5]{};
    if (!directory.Open(path).Succeeded() || !directory.OpenRead("asset", file).Succeeded() ||
        !file.Clone(clone).Succeeded())
    {
        return 10;
    }
    const auto read = clone.ReadAt(1, bytes);
    return read.Outcome.Succeeded() && read.BytesRead == 4 && bytes[0] == 's' ? 0 : 10;
}
