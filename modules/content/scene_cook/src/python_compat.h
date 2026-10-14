#pragma once

// Private helpers that reproduce the CPython 3.12 behaviour the retired
// scene pipeline relied on: float repr, json.dumps(indent=2), float()/int()
// string conversion and json.loads of string literals. Neumaier sum() lives in
// FoundationMath (math::CompensatedSum).

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/strings/string.hpp>

#include <span>
#include <string_view>

namespace ludus::content::scene_cook::detail
{
using namespace ludus::foundation;

/// Growable byte sink that remembers allocation failure.
class ByteWriter final
{
public:
    explicit ByteWriter(Array<uint8>& inoutBuffer) noexcept : mBuffer(inoutBuffer) {}
    void Append(std::string_view text) noexcept;
    void Append(char value) noexcept
    {
        Append(std::string_view(&value, 1));
    }
    void AppendBytes(std::span<const uint8> data) noexcept;
    void AppendUint32(uint32 value) noexcept;
    LUDUS_INLINE void AppendUint64(uint64 value) noexcept
    {
        AppendUint32(static_cast<uint32>(value));
        AppendUint32(static_cast<uint32>(value >> 32));
    }
    /// struct.pack('<f'): rounds to nearest; overflow to infinity is an error.
    void AppendFloat32(float64 value) noexcept;
    [[nodiscard]] bool IsOk() const noexcept
    {
        return mIsOk;
    }
    [[nodiscard]] bool HasOverflowed() const noexcept
    {
        return mHasOverflowed;
    }

private:
    Array<uint8>& mBuffer;
    bool mIsOk = true;
    bool mHasOverflowed = false;
};

/// Python float repr ('r' format with ".0" added to integral values).
void AppendFloatRepr(ByteWriter& inoutWriter, float64 value) noexcept;
/// Decimal text of a signed integer.
void AppendInteger(ByteWriter& inoutWriter, int64 value) noexcept;
/// json.dumps string with ensure_ascii=True; input must be valid UTF-8.
void AppendJsonString(ByteWriter& inoutWriter, std::string_view value) noexcept;

/// json.dumps(indent=2) emitter; callers supply keys and values in order.
class PythonJsonWriter final
{
public:
    explicit PythonJsonWriter(ByteWriter& inoutWriter) noexcept : mWriter(inoutWriter) {}
    void BeginObject() noexcept;
    void EndObject() noexcept;
    void BeginArray() noexcept;
    void EndArray() noexcept;
    void WriteKey(std::string_view key) noexcept;
    void WriteString(std::string_view value) noexcept;
    void WriteInteger(int64 value) noexcept;
    void WriteFloat(float64 value) noexcept;
    void WriteBoolean(bool isTrue) noexcept;
    void WriteNull() noexcept;

private:
    void beginValue() noexcept;
    void writeNewline() noexcept;
    ByteWriter& mWriter;
    usize mDepth = 0;
    bool mIsFirst = true;
    bool mIsAfterKey = false;
};

enum class NumberResult : uint8
{
    Ok,
    Syntax,
    Nonfinite,
    OutOfMemory,
};
/// float(text) for each token; nonfinite results are reported separately.
[[nodiscard]] NumberResult ParseFloats(std::span<const std::string_view> tokens, Array<float64>& outValues) noexcept;
/// int(text) bounded to int64; out-of-range values are a syntax failure.
[[nodiscard]] bool ParseInt(std::string_view text, int64& outValue) noexcept;
/// json.loads of one JSON string literal token.
[[nodiscard]] bool DecodeJsonString(std::string_view token, foundation::String& outText) noexcept;
/// Python str.isspace for one code point.
[[nodiscard]] bool IsPythonSpace(uint32 codePoint) noexcept;
/// Round half to even (Python round(float)).
[[nodiscard]] float64 RoundHalfEven(float64 value) noexcept;
/// Lowercase hexadecimal SHA-256 digest.
void AppendHex(ByteWriter& inoutWriter, std::span<const uint8> digest) noexcept;
} // namespace ludus::content::scene_cook::detail
