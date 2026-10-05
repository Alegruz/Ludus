#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/memory/allocation_domain.hpp>
#include <ludus/foundation/strings/status.hpp>

#include <string_view>

namespace ludus::foundation
{
class String;
// Thanks to The Qt Company, "Implicit Sharing", Qt 6 documentation,
// https://doc.qt.io/qt-6/implicit-sharing.html: adapt cheap shared copies, with
// an immutable API instead of mutation/detachment. See docs/architecture/strings.md.
// Immutable bytes shared by copies. Separate handles may be copied/destroyed on
// different threads after publication; concurrent access to the same handle is
// the caller's responsibility. Empty has no allocation. No copy-on-write.
class SharedString final
{
public:
    SharedString() noexcept = default;
    ~SharedString() noexcept;
    SharedString(const SharedString& other) noexcept;
    SharedString& operator=(const SharedString& other) noexcept;
    SharedString(SharedString&& other) noexcept;
    SharedString& operator=(SharedString&& other) noexcept;
    [[nodiscard]] usize GetSize() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept
    {
        return GetSize() == 0;
    }
    [[nodiscard]] std::string_view GetView() const& noexcept
    {
        return {GetData(), GetSize()};
    }
    [[nodiscard]] std::string_view GetView() const&& = delete;
    [[nodiscard]] const char* GetData() const& noexcept;
    [[nodiscard]] const char* GetData() const&& = delete;
    friend bool operator==(const SharedString& left, const SharedString& right) noexcept;

private:
    struct Block;
    Block* mBlock{};
    void Retain() const noexcept;
    void Release() noexcept;
    friend StringStatus CreateShared(std::string_view, const AllocationDomain&, SharedString&) noexcept;
};
// Output unchanged on failure, even when source borrows the old output.
[[nodiscard]] StringStatus
CreateShared(std::string_view value, const AllocationDomain& domain, SharedString& output) noexcept;
[[nodiscard]] StringStatus CopyToString(const SharedString& value, String& output) noexcept;
} // namespace ludus::foundation
