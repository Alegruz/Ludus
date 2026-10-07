#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::foundation::filesystem::detail
{
enum class AsyncFault : uint8
{
    State,
    Slots,
    Workers,
    Traces,
    Paths,
    Gate,
    Start
};
[[nodiscard]] bool FailAsync(AsyncFault point) noexcept;
} // namespace ludus::foundation::filesystem::detail
