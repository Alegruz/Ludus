#include <ludus/content/json.h>

#include <cstring>
#include <new>

#include <yyjson.h>

namespace ludus::content
{
namespace
{
yyjson_val* Val(JsonValue value) noexcept
{
    return const_cast<yyjson_val*>(static_cast<const yyjson_val*>(value.Value));
}
yyjson_val* Mutable(JsonValue value) noexcept
{
    return const_cast<yyjson_val*>(Val(value));
}
bool Check(yyjson_val* value, usize depth) noexcept
{
    if (depth > 32)
    {
        return false;
    }
    if (yyjson_is_obj(value))
    {
        if (yyjson_obj_size(value) > 32)
        {
            return false;
        }
        std::string_view keys[32];
        usize count = 0;
        usize index = 0, length = 0;
        yyjson_val *key = nullptr, *child = nullptr;
        yyjson_obj_foreach(value, index, length, key, child)
        {
            const std::string_view name{yyjson_get_str(key), yyjson_get_len(key)};
            for (usize i = 0; i < count; ++i)
            {
                if (keys[i] == name)
                {
                    return false;
                }
            }
            keys[count++] = name;
            if (!Check(child, depth + 1))
            {
                return false;
            }
        }
    }
    else if (yyjson_is_arr(value))
    {
        usize index = 0, length = 0;
        yyjson_val* child = nullptr;
        yyjson_arr_foreach(value, index, length, child)
        {
            if (!Check(child, depth + 1))
            {
                return false;
            }
        }
    }
    return true;
}
} // namespace
JsonValue JsonValue::Get(const char* key) const noexcept
{
    return {yyjson_obj_get(Mutable(*this), key)};
}
JsonValue JsonValue::At(usize index) const noexcept
{
    return {yyjson_arr_get(Mutable(*this), index)};
}
usize JsonValue::Count() const noexcept
{
    return yyjson_arr_size(Val(*this));
}
bool JsonValue::Array() const noexcept
{
    return yyjson_is_arr(Val(*this));
}
bool JsonValue::String(std::string_view& output) const noexcept
{
    if (!yyjson_is_str(Val(*this)))
    {
        return false;
    }
    output = {yyjson_get_str(Val(*this)), yyjson_get_len(Val(*this))};
    return true;
}
bool JsonValue::Integer(uint64& output) const noexcept
{
    if (!yyjson_is_uint(Val(*this)))
    {
        return false;
    }
    output = yyjson_get_uint(Val(*this));
    return true;
}
bool JsonValue::Number(float64& output) const noexcept
{
    if (!yyjson_is_num(Val(*this)))
    {
        return false;
    }
    output = yyjson_get_num(Val(*this));
    return true;
}
bool JsonValue::Boolean(bool& output) const noexcept
{
    if (!yyjson_is_bool(Val(*this)))
    {
        return false;
    }
    output = yyjson_get_bool(Val(*this));
    return true;
}
bool JsonValue::Fields(std::initializer_list<const char*> required) const noexcept
{
    if (!yyjson_is_obj(Val(*this)) || yyjson_obj_size(Val(*this)) != required.size())
    {
        return false;
    }
    for (const char* key : required)
    {
        if (Get(key).Value == nullptr)
        {
            return false;
        }
    }
    return true;
}
JsonDocument::~JsonDocument() noexcept
{
    if (mDocument != nullptr)
    {
        yyjson_doc_free(static_cast<yyjson_doc*>(mDocument));
    }
    delete[] mPool;
}
Status JsonDocument::Read(std::string_view input, Diagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (mDocument != nullptr || input.empty() || input.size() > MAX_DOCUMENT_BYTES)
    {
        return diagnostic.Result = Status::Limit;
    }
    const usize size = yyjson_read_max_memory_usage(input.size(), 0);
    mPool = new (std::nothrow) uint8[size];
    if (mPool == nullptr)
    {
        return diagnostic.Result = Status::OutOfMemory;
    }
    yyjson_alc allocator{};
    if (!yyjson_alc_pool_init(&allocator, mPool, size))
    {
        return diagnostic.Result = Status::OutOfMemory;
    }
    yyjson_read_err error{};
    mDocument = yyjson_read_opts(const_cast<char*>(input.data()), input.size(), 0, &allocator, &error);
    if (mDocument == nullptr)
    {
        diagnostic.Offset = error.pos;
        return diagnostic.Result = Status::Invalid;
    }
    if (!Check(yyjson_doc_get_root(static_cast<yyjson_doc*>(mDocument)), 0))
    {
        return diagnostic.Result = Status::Invalid;
    }
    return Status::Ok;
}
JsonValue JsonDocument::Root() const noexcept
{
    return {mDocument == nullptr ? nullptr : yyjson_doc_get_root(static_cast<yyjson_doc*>(mDocument))};
}
void JsonWriter::Raw(std::string_view value) noexcept
{
    if (value.size() > mOutput.size() - mSize)
    {
        mFailed = true;
        return;
    }
    for (char ch : value)
    {
        mOutput[mSize++] = static_cast<uint8>(ch);
    }
}
void JsonWriter::String(std::string_view value) noexcept
{
    Raw("\"");
    for (char ch : value)
    {
        if (ch == '"' || ch == '\\')
        {
            Raw("\\");
            Raw({&ch, 1});
        }
        else if (static_cast<uint8>(ch) < 32)
        {
            constexpr char hex[] = "0123456789abcdef";
            const char escaped[] =
                {'\\', 'u', '0', '0', hex[static_cast<uint8>(ch) >> 4], hex[static_cast<uint8>(ch) & 15]};
            Raw({escaped, 6});
        }
        else
        {
            Raw({&ch, 1});
        }
    }
    Raw("\"");
}
void JsonWriter::Integer(uint64 value) noexcept
{
    char data[32];
    usize pos = 32;
    do
    {
        data[--pos] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    Raw({data + pos, 32 - pos});
}
void JsonWriter::Number(float64 value) noexcept
{
    yyjson_mut_val number{};
    yyjson_mut_set_real(&number, value);
    usize length = 0;
    yyjson_write_err error{};
    uint8 pool[1024]{};
    yyjson_alc allocator{};
    (void)yyjson_alc_pool_init(&allocator, pool, sizeof(pool));
    char* text = yyjson_mut_val_write_opts(&number, 0, &allocator, &length, &error);
    if (text == nullptr || length > 64)
    {
        mFailed = true;
    }
    else
    {
        Raw({text, length});
    }
}
void JsonWriter::Boolean(bool value) noexcept
{
    Raw(value ? "true" : "false");
}
Status JsonWriter::Finish(Bytes& output) noexcept
{
    Raw("\n");
    if (mFailed)
    {
        return Status::Limit;
    }
    if (!output.Resize(mSize))
    {
        return Status::OutOfMemory;
    }
    std::memcpy(output.Data().data(), mOutput.data(), mSize);
    return Status::Ok;
}
} // namespace ludus::content
