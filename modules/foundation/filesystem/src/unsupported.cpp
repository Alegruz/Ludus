#include <ludus/foundation/filesystem/filesystem.hpp>

namespace ludus::foundation::filesystem
{
File::~File() noexcept
{
    (void)Close();
}
File::File(File&& other) noexcept : mImpl(other.mImpl)
{
    other.mImpl = nullptr;
}
File& File::operator=(File&& other) noexcept
{
    if (this != &other)
    {
        mImpl = other.mImpl;
        other.mImpl = nullptr;
    }
    return *this;
}
bool File::IsOpen() const noexcept
{
    return false;
}
uint64 File::Size() const noexcept
{
    return 0;
}
Result File::Clone(File& /*output*/) const noexcept
{
    return {Status::Unsupported};
}
ReadResult File::ReadAt(uint64 /*offset*/, std::span<uint8> /*destination*/) const noexcept
{
    return {{Status::Unsupported}};
}
Result File::Close() noexcept
{
    return {};
}
Directory::~Directory() noexcept
{
    (void)Close();
}
Directory::Directory(Directory&& other) noexcept : mImpl(other.mImpl)
{
    other.mImpl = nullptr;
}
Directory& Directory::operator=(Directory&& other) noexcept
{
    if (this != &other)
    {
        mImpl = other.mImpl;
        other.mImpl = nullptr;
    }
    return *this;
}
bool Directory::IsOpen() const noexcept
{
    return false;
}
Result Directory::Open(std::string_view /*nativeRoot*/) noexcept
{
    return {Status::Unsupported};
}
Result Directory::OpenRead(std::string_view /*relativePath*/, File& /*output*/) const noexcept
{
    return {Status::Unsupported};
}
Result Directory::Close() noexcept
{
    return {};
}
} // namespace ludus::foundation::filesystem
