#include <ludus/content/content.h>
#include <ludus/content/json.h>

#include <new>

namespace ludus::content
{
Bytes::~Bytes() noexcept
{
    delete[] mData;
}
Bytes::Bytes(Bytes&& other) noexcept : mData(other.mData), mSize(other.mSize)
{
    other.mData = nullptr;
    other.mSize = 0;
}
Bytes& Bytes::operator=(Bytes&& other) noexcept
{
    if (this != &other)
    {
        delete[] mData;
        mData = other.mData;
        mSize = other.mSize;
        other.mData = nullptr;
        other.mSize = 0;
    }
    return *this;
}
bool Bytes::Resize(usize size) noexcept
{
    auto* next = new (std::nothrow) uint8[size];
    if (next == nullptr)
    {
        return false;
    }
    delete[] mData;
    mData = next;
    mSize = size;
    return true;
}
bool ValidId(std::string_view value) noexcept
{
    if (value.empty() || value.size() > 128 || value.front() == '/' || value.back() == '/')
    {
        return false;
    }
    char previous = '\0';
    for (char ch : value)
    {
        if ((ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '-' && ch != '/')
        {
            return false;
        }
        if (ch == '/' && previous == '/')
        {
            return false;
        }
        previous = ch;
    }
    return true;
}
namespace
{
bool Utf8(std::string_view text) noexcept
{
    usize i = 0;
    while (i < text.size())
    {
        const auto lead = static_cast<uint8>(text[i++]);
        if (lead < 128)
        {
            continue;
        }
        const uint32 count = lead >= 0xc2 && lead <= 0xdf   ? 1
                             : lead >= 0xe0 && lead <= 0xef ? 2
                             : lead >= 0xf0 && lead <= 0xf4 ? 3
                                                            : 0;
        if (count == 0 || text.size() - i < count)
        {
            return false;
        }
        uint32 code = lead & ((1U << (6 - count)) - 1U);
        for (uint32 j = 0; j < count; ++j)
        {
            const auto next = static_cast<uint8>(text[i++]);
            if ((next & 0xc0U) != 0x80U)
            {
                return false;
            }
            code = (code << 6) | (next & 0x3fU);
        }
        if ((count == 1 && code < 128) || (count == 2 && code < 2048) || (count == 3 && code < 65536) ||
            code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
        {
            return false;
        }
    }
    return true;
}
} // namespace
bool ValidPath(std::string_view value) noexcept
{
    if (!Utf8(value) || value.empty() || value.size() > 1024 || value.front() == '/' || value.back() == '/')
    {
        return false;
    }
    usize begin = 0;
    for (usize i = 0; i <= value.size(); ++i)
    {
        if (i < value.size() && (value[i] == '\\' || value[i] == ':' || static_cast<uint8>(value[i]) < 32))
        {
            return false;
        }
        if (i == value.size() || value[i] == '/')
        {
            const auto segment = value.substr(begin, i - begin);
            if (segment.empty() || segment == "." || segment == "..")
            {
                return false;
            }
            begin = i + 1;
        }
    }
    return true;
}
Catalog::Catalog(Catalog&& other) noexcept : mEntries(other.mEntries), mCount(other.mCount)
{
    other.mEntries = nullptr;
    other.mCount = 0;
}
Catalog& Catalog::operator=(Catalog&& other) noexcept
{
    if (this != &other)
    {
        delete[] mEntries;
        mEntries = other.mEntries;
        mCount = other.mCount;
        other.mEntries = nullptr;
        other.mCount = 0;
    }
    return *this;
}
Catalog::~Catalog() noexcept
{
    delete[] mEntries;
}
const Resource* Catalog::Find(std::string_view id) const noexcept
{
    for (usize i = 0; i < mCount; ++i)
    {
        if (mEntries[i].Id.View() == id)
        {
            return &mEntries[i];
        }
    }
    return nullptr;
}
Status Catalog::Read(std::string_view json, Diagnostic& diagnostic) noexcept
{
    JsonDocument document;
    auto status = document.Read(json, diagnostic);
    if (status != Status::Ok)
    {
        return status;
    }
    const auto root = document.Root();
    uint64 version = 0;
    if (!root.Fields({"version", "resources"}) || !root.Get("version").Integer(version) || version != 1)
    {
        return diagnostic.Result = Status::Invalid;
    }
    const auto entries = root.Get("resources");
    const usize count = entries.Count();
    if (!entries.Array() || count > MAX_RESOURCES)
    {
        return diagnostic.Result = Status::Limit;
    }
    auto* next = new (std::nothrow) Resource[count];
    if (next == nullptr)
    {
        return diagnostic.Result = Status::OutOfMemory;
    }
    for (usize i = 0; i < count; ++i)
    {
        const auto item = entries.At(i);
        std::string_view id, path, kind;
        bool valid = item.Fields({"id", "kind", "path"}) && item.Get("id").String(id) && ValidId(id) &&
                     item.Get("path").String(path) && ValidPath(path) && item.Get("kind").String(kind);
        if (valid)
        {
            if (kind == "audio-source")
            {
                next[i].Type = Kind::AudioSource;
            }
            else if (kind == "sound")
            {
                next[i].Type = Kind::Sound;
            }
            else if (kind == "music")
            {
                next[i].Type = Kind::Music;
            }
            else
            {
                valid = false;
            }
        }
        for (usize j = 0; valid && j < i; ++j)
        {
            if (next[j].Id.View() == id)
            {
                valid = false;
            }
        }
        if (!valid)
        {
            delete[] next;
            return diagnostic.Result = Status::Invalid;
        }
        (void)next[i].Id.Set(id);
        (void)next[i].Path.Set(path);
    }
    // Stable IDs, independent of source array ordering.
    for (usize i = 1; i < count; ++i)
    {
        const auto item = next[i];
        usize pos = i;
        while (pos > 0 && next[pos - 1].Id.View() > item.Id.View())
        {
            next[pos] = next[pos - 1];
            --pos;
        }
        next[pos] = item;
    }
    delete[] mEntries;
    mEntries = next;
    mCount = count;
    return Status::Ok;
}
Status Catalog::Put(const Resource& resource) noexcept
{
    if ((resource.Type != Kind::AudioSource && resource.Type != Kind::Sound && resource.Type != Kind::Music) ||
        !ValidId(resource.Id.View()) || !ValidPath(resource.Path.View()))
    {
        return Status::Invalid;
    }
    const auto* existing = Find(resource.Id.View());
    if (existing != nullptr)
    {
        return existing->Path.View() == resource.Path.View() && existing->Type == resource.Type ? Status::Ok
                                                                                                : Status::Conflict;
    }
    if (mCount >= MAX_RESOURCES)
    {
        return Status::Limit;
    }
    auto* next = new (std::nothrow) Resource[mCount + 1];
    if (next == nullptr)
    {
        return Status::OutOfMemory;
    }
    for (usize i = 0; i < mCount; ++i)
    {
        next[i] = mEntries[i];
    }
    next[mCount] = resource;
    usize pos = mCount;
    while (pos > 0 && next[pos - 1].Id.View() > resource.Id.View())
    {
        next[pos] = next[pos - 1];
        --pos;
    }
    next[pos] = resource;
    delete[] mEntries;
    mEntries = next;
    ++mCount;
    return Status::Ok;
}
Status Catalog::Write(Bytes& output) const noexcept
{
    Bytes buffer;
    if (!buffer.Resize(MAX_DOCUMENT_BYTES))
    {
        return Status::OutOfMemory;
    }
    JsonWriter writer(buffer.Data());
    writer.Raw("{\n  \"version\": 1,\n  \"resources\": [");
    for (usize i = 0; i < mCount; ++i)
    {
        const auto& r = mEntries[i];
        writer.Raw(i == 0 ? "\n    { \"id\": " : ",\n    { \"id\": ");
        writer.String(r.Id.View());
        writer.Raw(", \"kind\": ");
        writer.String(r.Type == Kind::Sound ? "sound" : r.Type == Kind::Music ? "music" : "audio-source");
        writer.Raw(", \"path\": ");
        writer.String(r.Path.View());
        writer.Raw(" }");
    }
    writer.Raw("\n  ]\n}");
    return writer.Finish(output);
}
} // namespace ludus::content
