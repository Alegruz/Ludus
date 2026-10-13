#pragma once

// Private helpers that reproduce the CPython 3.12 behaviour the retired
// scene pipeline relied on: float repr, json.dumps(indent=2), float()/int()
// string conversion, json.loads of string literals and Neumaier sum().

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/strings/string.hpp>

#include <span>
#include <string_view>

namespace ludus::content::scene_cook::detail
{
using namespace ludus::foundation;

/// Growable byte sink that remembers allocation failure.
class Sink final
{
public:
    explicit Sink(Array<uint8>& output) noexcept : mOutput(output) {}
    void Append(std::string_view text) noexcept;
    void Append(char value) noexcept
    {
        Append(std::string_view(&value, 1));
    }
    void Bytes(std::span<const uint8> data) noexcept;
    void U32(uint32 value) noexcept;
    /// struct.pack('<f'): rounds to nearest; overflow to infinity is an error.
    void F32(float64 value) noexcept;
    [[nodiscard]] bool Ok() const noexcept
    {
        return mOk;
    }
    [[nodiscard]] bool Overflow() const noexcept
    {
        return mOverflow;
    }

private:
    Array<uint8>& mOutput;
    bool mOk = true;
    bool mOverflow = false;
};

/// Python float repr ('r' format with ".0" added to integral values).
void AppendFloatRepr(Sink& sink, float64 value) noexcept;
/// Decimal text of a signed integer.
void AppendInteger(Sink& sink, int64 value) noexcept;
/// json.dumps string with ensure_ascii=True; input must be valid UTF-8.
void AppendJsonString(Sink& sink, std::string_view value) noexcept;

/// json.dumps(indent=2) emitter; callers supply keys and values in order.
class PyJson final
{
public:
    explicit PyJson(Sink& sink) noexcept : mSink(sink) {}
    void BeginObject() noexcept;
    void EndObject() noexcept;
    void BeginArray() noexcept;
    void EndArray() noexcept;
    void Key(std::string_view key) noexcept;
    void String(std::string_view value) noexcept;
    void Integer(int64 value) noexcept;
    void Float(float64 value) noexcept;
    void Boolean(bool value) noexcept;
    void Null() noexcept;

private:
    void BeforeValue() noexcept;
    void Newline() noexcept;
    Sink& mSink;
    usize mDepth = 0;
    bool mFirst = true;
    bool mAfterKey = false;
};

/// CPython 3.12 sum() over floats (Neumaier compensation, start 0).
struct PySum final
{
    float64 Total = 0.0;
    float64 Compensation = 0.0;
    void Add(float64 value) noexcept;
    [[nodiscard]] float64 Result() const noexcept;
};

enum class NumberResult : uint8
{
    Ok,
    Syntax,
    Nonfinite,
    OutOfMemory,
};
/// float(text) for each token; nonfinite results are reported separately.
[[nodiscard]] NumberResult ParseFloats(std::span<const std::string_view> tokens, Array<float64>& output) noexcept;
/// int(text) bounded to int64; out-of-range values are a syntax failure.
[[nodiscard]] bool ParseInt(std::string_view text, int64& output) noexcept;
/// json.loads of one JSON string literal token.
[[nodiscard]] bool DecodeJsonString(std::string_view token, foundation::String& output) noexcept;
/// Python str.isspace for one code point.
[[nodiscard]] bool IsPySpace(uint32 codePoint) noexcept;
/// Round half to even (Python round(float)).
[[nodiscard]] float64 RoundHalfEven(float64 value) noexcept;
/// Lowercase hexadecimal SHA-256 digest.
void AppendHex(Sink& sink, std::span<const uint8> digest) noexcept;
} // namespace ludus::content::scene_cook::detail
