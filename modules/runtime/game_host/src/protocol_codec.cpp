#include "internal/protocol_codec.h"

#include <ludus/foundation/base/core.h>

#include <cstdio>
#include <cstring>

namespace ludus::runtime::game_host::protocol
{
namespace
{
using ludus::foundation::uint8;

void AppendEscaped(std::string& out, std::string_view value)
{
    out.push_back('"');
    for (const char c : value)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<uint8>(c) < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<uint8>(c)));
                    out += buf;
                }
                else
                {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
}

// Minimal recursive-free scanner state over a compact JSON object.
struct Scanner
{
    std::string_view text;
    usize pos = 0;

    void SkipWs()
    {
        while (pos < text.size())
        {
            const char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                ++pos;
            }
            else
            {
                break;
            }
        }
    }

    [[nodiscard]] bool AtEnd()
    {
        SkipWs();
        return pos >= text.size();
    }

    [[nodiscard]] bool Expect(char c)
    {
        SkipWs();
        if (pos < text.size() && text[pos] == c)
        {
            ++pos;
            return true;
        }
        return false;
    }

    // Parse a JSON string into out. Rejects nested escapes it does not support.
    [[nodiscard]] bool ParseString(std::string& out)
    {
        SkipWs();
        if (pos >= text.size() || text[pos] != '"')
        {
            return false;
        }
        ++pos;
        out.clear();
        while (pos < text.size())
        {
            const char c = text[pos++];
            if (c == '"')
            {
                return true;
            }
            if (c == '\\')
            {
                if (pos >= text.size())
                {
                    return false;
                }
                const char e = text[pos++];
                switch (e)
                {
                    case '"':
                        out.push_back('"');
                        break;
                    case '\\':
                        out.push_back('\\');
                        break;
                    case '/':
                        out.push_back('/');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    case 'u': {
                        if (pos + 4 > text.size())
                        {
                            return false;
                        }
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            const char h = text[pos++];
                            code <<= 4;
                            if (h >= '0' && h <= '9')
                            {
                                code |= static_cast<unsigned>(h - '0');
                            }
                            else if (h >= 'a' && h <= 'f')
                            {
                                code |= static_cast<unsigned>(h - 'a' + 10);
                            }
                            else if (h >= 'A' && h <= 'F')
                            {
                                code |= static_cast<unsigned>(h - 'A' + 10);
                            }
                            else
                            {
                                return false;
                            }
                        }
                        // Only basic control chars are produced by the writer.
                        if (code < 0x80)
                        {
                            out.push_back(static_cast<char>(code));
                        }
                        else
                        {
                            return false;
                        }
                        break;
                    }
                    default:
                        return false;
                }
            }
            else
            {
                out.push_back(c);
            }
            if (out.size() > kMaxStringFieldBytes)
            {
                return false;
            }
        }
        return false;
    }

    // Parse a bare token (number or true/false) up to a delimiter.
    [[nodiscard]] bool ParseBare(std::string& out)
    {
        SkipWs();
        out.clear();
        while (pos < text.size())
        {
            const char c = text[pos];
            if (c == ',' || c == '}' || c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                break;
            }
            out.push_back(c);
            ++pos;
            if (out.size() > 64)
            {
                return false;
            }
        }
        return !out.empty();
    }
};

[[nodiscard]] bool IsNumber(std::string_view token)
{
    if (token.empty())
    {
        return false;
    }
    usize i = 0;
    if (token[0] == '-')
    {
        if (token.size() == 1)
        {
            return false;
        }
        i = 1;
    }
    for (; i < token.size(); ++i)
    {
        if (token[i] < '0' || token[i] > '9')
        {
            return false;
        }
    }
    return true;
}
} // namespace

const Message::Field* Message::Find(std::string_view key) const
{
    for (const Field& f : Fields_)
    {
        if (f.Key == key)
        {
            return &f;
        }
    }
    return nullptr;
}

void Message::SetString(std::string_view key, std::string_view value)
{
    Fields_.push_back(Field{std::string(key), std::string(value), ValueType::String});
}

void Message::SetUint(std::string_view key, uint64 value)
{
    Fields_.push_back(Field{std::string(key), std::to_string(value), ValueType::Number});
}

void Message::SetInt(std::string_view key, int64 value)
{
    Fields_.push_back(Field{std::string(key), std::to_string(value), ValueType::Number});
}

void Message::SetBool(std::string_view key, bool value)
{
    Fields_.push_back(Field{std::string(key), value ? "true" : "false", ValueType::Bool});
}

void Message::SetHexId(std::string_view key, uint64 value)
{
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
    Fields_.push_back(Field{std::string(key), std::string(buf, 16), ValueType::String});
}

bool Message::GetString(std::string_view key, std::string& out) const
{
    const Field* f = Find(key);
    if (f == nullptr || f->Type != ValueType::String)
    {
        return false;
    }
    out = f->Value;
    return true;
}

bool Message::GetUint(std::string_view key, uint64& out) const
{
    const Field* f = Find(key);
    if (f == nullptr || f->Type != ValueType::Number)
    {
        return false;
    }
    uint64 value = 0;
    for (const char c : f->Value)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
        value = value * 10 + static_cast<uint64>(c - '0');
    }
    out = value;
    return true;
}

bool Message::GetInt(std::string_view key, int64& out) const
{
    const Field* f = Find(key);
    if (f == nullptr || f->Type != ValueType::Number)
    {
        return false;
    }
    bool negative = false;
    std::string_view digits = f->Value;
    if (!digits.empty() && digits[0] == '-')
    {
        negative = true;
        digits.remove_prefix(1);
    }
    int64 value = 0;
    for (const char c : digits)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out = negative ? -value : value;
    return true;
}

bool Message::GetBool(std::string_view key, bool& out) const
{
    const Field* f = Find(key);
    if (f == nullptr || f->Type != ValueType::Bool)
    {
        return false;
    }
    out = (f->Value == "true");
    return true;
}

bool Message::GetHexId(std::string_view key, uint64& out) const
{
    std::string text;
    if (!GetString(key, text) || text.size() != 16)
    {
        return false;
    }
    uint64 value = 0;
    for (const char c : text)
    {
        value <<= 4;
        if (c >= '0' && c <= '9')
        {
            value |= static_cast<uint64>(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            value |= static_cast<uint64>(c - 'a' + 10);
        }
        else
        {
            return false;
        }
    }
    out = value;
    return true;
}

bool Message::Has(std::string_view key) const
{
    return Find(key) != nullptr;
}

std::string Message::Serialize() const
{
    std::string out;
    out.push_back('{');
    bool first = true;
    for (const Field& f : Fields_)
    {
        if (!first)
        {
            out.push_back(',');
        }
        first = false;
        AppendEscaped(out, f.Key);
        out.push_back(':');
        if (f.Type == ValueType::String)
        {
            AppendEscaped(out, f.Value);
        }
        else
        {
            out += f.Value;
        }
    }
    out.push_back('}');
    return out;
}

bool Message::Parse(std::string_view json, Message& out)
{
    out.Fields_.clear();
    Scanner s{json, 0};
    if (!s.Expect('{'))
    {
        return false;
    }
    s.SkipWs();
    if (s.pos < s.text.size() && s.text[s.pos] == '}')
    {
        ++s.pos;
        return s.AtEnd();
    }
    for (;;)
    {
        if (out.Fields_.size() >= kMaxFields)
        {
            return false;
        }
        std::string key;
        if (!s.ParseString(key))
        {
            return false;
        }
        if (!s.Expect(':'))
        {
            return false;
        }
        s.SkipWs();
        if (s.pos >= s.text.size())
        {
            return false;
        }
        Field field;
        field.Key = key;
        if (s.text[s.pos] == '"')
        {
            if (!s.ParseString(field.Value))
            {
                return false;
            }
            field.Type = ValueType::String;
        }
        else
        {
            std::string token;
            if (!s.ParseBare(token))
            {
                return false;
            }
            if (token == "true" || token == "false")
            {
                field.Type = ValueType::Bool;
                field.Value = token;
            }
            else if (IsNumber(token))
            {
                field.Type = ValueType::Number;
                field.Value = token;
            }
            else
            {
                return false; // null / nested / unsupported
            }
        }
        out.Fields_.push_back(std::move(field));
        if (s.Expect(','))
        {
            continue;
        }
        if (s.Expect('}'))
        {
            break;
        }
        return false;
    }
    return s.AtEnd();
}

bool EncodeFrame(std::string_view payload, std::vector<uint8>& outFrame)
{
    if (payload.size() > kMaxControlFrameBytes)
    {
        return false;
    }
    const auto length = static_cast<uint32>(payload.size());
    outFrame.clear();
    outFrame.reserve(payload.size() + 4);
    outFrame.push_back(static_cast<uint8>(length & 0xFF));
    outFrame.push_back(static_cast<uint8>((length >> 8) & 0xFF));
    outFrame.push_back(static_cast<uint8>((length >> 16) & 0xFF));
    outFrame.push_back(static_cast<uint8>((length >> 24) & 0xFF));
    const auto* bytes = reinterpret_cast<const uint8*>(payload.data());
    outFrame.insert(outFrame.end(), bytes, bytes + payload.size());
    return true;
}

void FrameReader::Append(const uint8* data, usize size)
{
    if (Failed_ || data == nullptr)
    {
        return;
    }
    if (Buffer_.size() + size > kMaxCommandQueueBytes)
    {
        Failed_ = true;
        return;
    }
    Buffer_.insert(Buffer_.end(), data, data + size);
}

bool FrameReader::Next(std::string& outPayload)
{
    if (Failed_ || Buffer_.size() < 4)
    {
        return false;
    }
    const uint32 length = static_cast<uint32>(Buffer_[0]) | (static_cast<uint32>(Buffer_[1]) << 8) |
                          (static_cast<uint32>(Buffer_[2]) << 16) | (static_cast<uint32>(Buffer_[3]) << 24);
    if (length > kMaxControlFrameBytes)
    {
        Failed_ = true;
        return false;
    }
    if (Buffer_.size() < 4 + static_cast<usize>(length))
    {
        return false; // incomplete; wait for more bytes
    }
    outPayload.assign(reinterpret_cast<const char*>(Buffer_.data() + 4), length);
    Buffer_.erase(Buffer_.begin(), Buffer_.begin() + 4 + static_cast<std::ptrdiff_t>(length));
    return true;
}
} // namespace ludus::runtime::game_host::protocol
