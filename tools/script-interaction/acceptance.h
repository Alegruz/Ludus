#pragma once
#include <ludus/foundation/base/core.h>

#include "shared.h"

namespace ludus::s1
{
[[nodiscard]] bool Check(bool success, const char* name) noexcept;
[[nodiscard]] bool CommonAcceptance(Handler handler, void* user) noexcept;
[[nodiscard]] bool DomainAcceptance() noexcept;
[[nodiscard]] bool CommonFaultAcceptance(Handler handler, void* user) noexcept;
[[nodiscard]] EventRecord Event(const World& world, uint32 instance, uint64 sequence, uint32 amount = 1) noexcept;
} // namespace ludus::s1
