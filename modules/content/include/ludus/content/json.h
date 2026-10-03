#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/content/content.h>

#include <initializer_list>
#include <string_view>

namespace ludus::content
{
// Borrowed views valid only during their Document lifetime. Runtime definitions
// copy validated fields; neither yyjson objects nor allocations escape the codec.
struct JsonValue final
{
    const void* Value = nullptr;
    [[nodiscard]] JsonValue Get(const char* key) const noexcept;
    [[nodiscard]] JsonValue At(usize index) const noexcept;
    [[nodiscard]] usize Count() const noexcept;
    [[nodiscard]] bool Array() const noexcept;
    [[nodiscard]] bool String(std::string_view& output) const noexcept;
    [[nodiscard]] bool Integer(uint64& output) const noexcept;
    [[nodiscard]] bool Number(float64& output) const noexcept;
    [[nodiscard]] bool Boolean(bool& output) const noexcept;
    [[nodiscard]] bool Fields(std::initializer_list<const char*> required) const noexcept;
};
class JsonDocument final
{
public:
    JsonDocument() noexcept = default;
    ~JsonDocument() noexcept;
    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;
    [[nodiscard]] Status Read(std::string_view input, Diagnostic& diagnostic) noexcept;
    [[nodiscard]] JsonValue Root() const noexcept;

private:
    void* mDocument = nullptr;
    uint8* mPool = nullptr;
};
// Fixed-capacity canonical writer. Numeric spelling/escaping use the same pinned
// C codec. Overflow reports failure and never returns a truncated document.
class JsonWriter final
{
public:
    explicit JsonWriter(std::span<uint8> output) noexcept : mOutput(output) {}
    void Raw(std::string_view value) noexcept;
    void String(std::string_view value) noexcept;
    void Integer(uint64 value) noexcept;
    void Number(float64 value) noexcept;
    void Boolean(bool value) noexcept;
    [[nodiscard]] Status Finish(Bytes& output) noexcept;

private:
    std::span<uint8> mOutput;
    usize mSize = 0;
    bool mFailed = false;
};
} // namespace ludus::content
