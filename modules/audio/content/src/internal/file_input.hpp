#pragma once
#include <ludus/audio/audio_source.h>
#include <ludus/content/content.h>

#include <new>
namespace ludus::audio::content::internal
{
class FileInput final : public StreamInput
{
public:
    ludus::content::FileReader File;
    Status Read(std::span<uint8> bytes, usize& count) noexcept override
    {
        return File.Read(bytes, count) == ludus::content::Status::Ok ? Status::Ok : Status::IoError;
    }
    Status Seek(foundation::int64 offset, bool relative) noexcept override
    {
        return File.Seek(offset, relative) == ludus::content::Status::Ok ? Status::Ok : Status::IoError;
    }
    [[nodiscard]] StreamInput* Clone() const noexcept
    {
        auto* next = new (std::nothrow) FileInput();
        if (next == nullptr)
        {
            return nullptr;
        }
        if (File.Clone(next->File) != ludus::content::Status::Ok)
        {
            delete next;
            return nullptr;
        }
        return next;
    }
};
} // namespace ludus::audio::content::internal
