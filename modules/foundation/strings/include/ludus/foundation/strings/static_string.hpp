#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/strings/status.hpp>

#include <string_view>

namespace ludus::foundation
{
namespace detail
{
[[nodiscard]] StringStatus
AssignBounded(char* destination, usize& size, usize capacity, std::string_view source) noexcept;
[[nodiscard]] StringStatus
AppendBounded(char* destination, usize& size, usize capacity, std::string_view source) noexcept;
} // namespace detail

// Capacity excludes the extra terminator. Counted operations preserve embedded
// zero bytes. Overflow preserves the entire old value; no silent truncation.
template <usize Capacity>
class StaticString final
{
    static_assert(Capacity < static_cast<usize>(-1));

public:
    [[nodiscard]] constexpr usize GetSize() const noexcept
    {
        return mSize;
    }
    [[nodiscard]] static constexpr usize GetCapacity() noexcept
    {
        return Capacity;
    }
    [[nodiscard]] constexpr bool IsEmpty() const noexcept
    {
        return mSize == 0;
    }
    [[nodiscard]] std::string_view GetView() const& noexcept
    {
        return {mBytes, mSize};
    }
    [[nodiscard]] std::string_view GetView() const&& = delete;
    [[nodiscard]] const char* GetData() const& noexcept
    {
        return mBytes;
    }
    [[nodiscard]] const char* GetData() const&& = delete;
    [[nodiscard]] StringStatus TryAssign(std::string_view value) noexcept
    {
        return detail::AssignBounded(mBytes, mSize, Capacity, value);
    }
    [[nodiscard]] StringStatus TryAppend(std::string_view value) noexcept
    {
        return detail::AppendBounded(mBytes, mSize, Capacity, value);
    }
    void Clear() noexcept
    {
        mSize = 0;
        mBytes[0] = '\0';
    }

private:
    usize mSize{};
    char mBytes[Capacity + 1]{};
};
} // namespace ludus::foundation
