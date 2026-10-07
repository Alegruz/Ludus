#include <ludus/content/content.h>

#include <cstdlib>
#include <string_view>

#include <unistd.h>

int ExerciseInstalledContent() noexcept
{
    using namespace ludus::content;
    char root[] = "/tmp/ludus-sdk-content-XXXXXX";
    if (::mkdtemp(root) == nullptr)
    {
        return 20;
    }
    struct Cleanup final
    {
        const char* Root;
        ~Cleanup() noexcept
        {
            Text<128> path;
            (void)path.Set(Root);
            const auto length = path.Length;
            for (char ch : std::string_view("/data"))
            {
                path.Data[path.Length++] = ch;
            }
            path.Data[path.Length] = '\0';
            (void)::unlink(path.Data);
            path.Data[length] = '\0';
            (void)::rmdir(path.Data);
        }
    } cleanup{root};
    const uint8 initial[] = {'a', 'b', 'c'}, replacement[] = {'x', 'y'};
    Bytes bytes;
    FileReader retained;
    if (SaveFile(root, "data", initial, nullptr) != Status::Ok || ReadFile(root, "data", 3, bytes) != Status::Ok ||
        bytes.String() != "abc" || retained.Open(root, "data") != Status::Ok)
    {
        return 20;
    }
    const auto digest = Hash(bytes.Data());
    if (SaveFile(root, "data", replacement, &digest) != Status::Ok ||
        SaveFile(root, "data", initial, &digest) != Status::Conflict ||
        SaveFile(root, "data", initial, nullptr) != Status::Conflict ||
        ReadFile(root, "data", 1, bytes) != Status::Limit || bytes.String() != "abc" ||
        ReadFile(root, "data", 2, bytes) != Status::Ok || bytes.String() != "xy")
    {
        return 20;
    }
    uint8 old[3]{};
    usize count = 0;
    if (retained.Read(old, count) != Status::Ok || count != 3 || old[0] != 'a')
    {
        return 20;
    }
    const auto next = Hash(bytes.Data());
    return SaveFile(root, "data", {}, &next) == Status::Ok && ReadFile(root, "data", 0, bytes) == Status::Ok &&
                   bytes.Data().empty()
               ? 0
               : 20;
}

#if defined(LUDUS_SDK_CONTENT_STANDALONE)
int main()
{
    return ExerciseInstalledContent();
}
#endif
