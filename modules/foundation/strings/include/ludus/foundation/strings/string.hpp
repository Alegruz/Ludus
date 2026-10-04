#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/memory/allocation_domain.hpp>
#include <ludus/foundation/strings/status.hpp>

#include <span>
#include <string_view>

namespace ludus::foundation
{
// Unique, move-only storage. The domain must outlive this owner. Explicit clone
// makes deep-copy allocation and failure visible. Moves carry the original
// domain; assignment of bytes retains the destination domain.
class String final
{
public:
    static constexpr usize kInlineCapacity = 23;
    explicit String(const AllocationDomain& domain = GetSystemAllocationDomain()) noexcept;
    ~String() noexcept;
    String(String&& other) noexcept;
    String& operator=(String&& other) noexcept;
    String(const String&) = delete;
    String& operator=(const String&) = delete;

    [[nodiscard]] usize GetSize() const noexcept
    {
        return mSize;
    }
    [[nodiscard]] usize GetCapacity() const noexcept
    {
        return mCapacity;
    }
    [[nodiscard]] bool IsEmpty() const noexcept
    {
        return mSize == 0;
    }
    [[nodiscard]] const AllocationDomain& GetDomain() const noexcept
    {
        return *mDomain;
    }
    [[nodiscard]] std::string_view GetView() const& noexcept
    {
        return {GetData(), mSize};
    }
    [[nodiscard]] std::string_view GetView() const&& = delete;
    [[nodiscard]] const char* GetData() const& noexcept;
    [[nodiscard]] const char* GetData() const&& = delete;

    [[nodiscard]] StringStatus TryAssign(std::string_view value) noexcept;
    [[nodiscard]] StringStatus TryAppend(std::string_view value) noexcept;
    // One reserve/commit for the batch, including self-borrowed slices.
    [[nodiscard]] StringStatus TryAppendMany(std::span<const std::string_view> values) noexcept;
    [[nodiscard]] StringStatus TryEnsureCapacity(usize capacity) noexcept;
    [[nodiscard]] StringStatus CloneTo(const AllocationDomain& domain, String& output) const noexcept;
    void Clear() noexcept;

private:
    [[nodiscard]] char* MutableData() noexcept;
    void Release() noexcept;
    const AllocationDomain* mDomain;
    usize mSize{};
    usize mCapacity{kInlineCapacity};
    union Storage
    {
        char Inline[kInlineCapacity + 1];
        char* Heap;
        constexpr Storage() noexcept : Inline{} {}
    } mStorage;
};

// Reusable append buffer; no expression templates or hidden copy allocation.
class StringBuilder final
{
public:
    explicit StringBuilder(const AllocationDomain& domain = GetSystemAllocationDomain()) noexcept : mString(domain) {}
    [[nodiscard]] StringStatus TryAppend(std::string_view value) noexcept
    {
        return mString.TryAppend(value);
    }
    [[nodiscard]] StringStatus TryAppendMany(std::span<const std::string_view> values) noexcept
    {
        return mString.TryAppendMany(values);
    }
    [[nodiscard]] StringStatus TryEnsureCapacity(usize capacity) noexcept
    {
        return mString.TryEnsureCapacity(capacity);
    }
    [[nodiscard]] std::string_view GetView() const& noexcept
    {
        return mString.GetView();
    }
    [[nodiscard]] std::string_view GetView() const&& = delete;
    [[nodiscard]] String TakeString() noexcept;
    void Clear() noexcept
    {
        mString.Clear();
    }

private:
    String mString;
};
} // namespace ludus::foundation
