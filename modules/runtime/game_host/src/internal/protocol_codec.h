#pragma once

// Bounded framing + minimal JSON codec for the play-session protocol
// (project-live-reload design 10). The protocol frames are small, flat objects
// (string/number/bool fields), so a hand-written bounded codec is used instead
// of pulling a heavy JSON dependency into the Qt-free host. This is NOT a
// general JSON library; it accepts exactly the closed, versioned schema and
// rejects anything else as a protocol error.

#include <ludus/runtime/game_host/protocol.h>

#include <ludus/foundation/base/types.h>

#include <string>
#include <string_view>
#include <vector>

namespace ludus::runtime::game_host::protocol
{
using ludus::foundation::int64;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::usize;

// A decoded flat JSON object: ordered key/value pairs, values typed. Bounded in
// count and total size by the caller before use.
class Message final
{
public:
    void SetString(std::string_view key, std::string_view value);
    void SetUint(std::string_view key, uint64 value);
    void SetInt(std::string_view key, int64 value);
    void SetBool(std::string_view key, bool value);
    // Hex-encoded 64-bit id (fixed 16 lowercase hex chars) to avoid float loss.
    void SetHexId(std::string_view key, uint64 value);

    [[nodiscard]] bool GetString(std::string_view key, std::string& out) const;
    [[nodiscard]] bool GetUint(std::string_view key, uint64& out) const;
    [[nodiscard]] bool GetInt(std::string_view key, int64& out) const;
    [[nodiscard]] bool GetBool(std::string_view key, bool& out) const;
    [[nodiscard]] bool GetHexId(std::string_view key, uint64& out) const;
    [[nodiscard]] bool Has(std::string_view key) const;

    // Serialize to compact UTF-8 JSON (no trailing newline).
    [[nodiscard]] std::string Serialize() const;

    // Parse a compact JSON object. Returns false on any malformed input, a
    // non-object top level, nested structures, or more than kMaxFields fields.
    [[nodiscard]] static bool Parse(std::string_view json, Message& out);

    [[nodiscard]] usize FieldCount() const noexcept
    {
        return Fields_.size();
    }

private:
    enum class ValueType : ludus::foundation::uint8
    {
        String,
        Number,
        Bool
    };
    struct Field
    {
        std::string Key;
        std::string Value; // raw: string text, decimal digits, or true/false
        ValueType Type = ValueType::String;
    };
    static constexpr usize kMaxFields = 32;
    [[nodiscard]] const Field* Find(std::string_view key) const;
    std::vector<Field> Fields_;
};

// Frame a serialized message with a 4-byte little-endian length prefix.
// Returns false if the payload exceeds kMaxControlFrameBytes.
[[nodiscard]] bool EncodeFrame(std::string_view payload, std::vector<ludus::foundation::uint8>& outFrame);

// Incremental frame reader over a byte stream. Appends bytes, then pops whole
// frames. Enforces the per-frame and backlog bounds; a violation latches an
// error that the caller treats as a protocol failure (design 10).
class FrameReader final
{
public:
    void Append(const ludus::foundation::uint8* data, usize size);
    // Pop one complete frame payload if available. Returns true and fills out.
    [[nodiscard]] bool Next(std::string& outPayload);
    [[nodiscard]] bool Failed() const noexcept
    {
        return Failed_;
    }
    [[nodiscard]] usize Backlog() const noexcept
    {
        return Buffer_.size();
    }

private:
    std::vector<ludus::foundation::uint8> Buffer_;
    bool Failed_ = false;
};
} // namespace ludus::runtime::game_host::protocol
