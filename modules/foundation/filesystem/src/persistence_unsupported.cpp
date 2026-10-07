#include <ludus/foundation/filesystem/persistence.hpp>

namespace ludus::foundation::filesystem
{
WriteDirectory::~WriteDirectory() noexcept
{
    (void)Close();
}
WriteDirectory::WriteDirectory(WriteDirectory&& other) noexcept : mImpl(other.mImpl)
{
    other.mImpl = nullptr;
}
WriteDirectory& WriteDirectory::operator=(WriteDirectory&& other) noexcept
{
    if (this != &other)
    {
        mImpl = other.mImpl;
        other.mImpl = nullptr;
    }
    return *this;
}
Result WriteDirectory::Open(std::string_view /*nativeRoot*/) noexcept
{
    return {Status::Unsupported};
}
bool WriteDirectory::IsOpen() const noexcept
{
    return false;
}
PublicationResult WriteDirectory::Publish(std::string_view /*relativePath*/,
                                          std::span<const uint8> /*bytes*/,
                                          const WriteOptions& /*options*/) const noexcept
{
    return {{Status::Unsupported}};
}
Result WriteDirectory::Close() noexcept
{
    return {};
}
} // namespace ludus::foundation::filesystem
