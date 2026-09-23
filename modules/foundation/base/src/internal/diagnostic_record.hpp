#pragma once

#include <ludus/foundation/base/diagnostic_output.hpp>

#include <atomic>

namespace ludus::foundation::diagnostics::internal
{
// Debugger/core evidence for one terminal incident. Readers acquire each ready
// flag before inspecting that immutable region. Checks never write this packet;
// secondary fatal failures never overwrite it. No live collector API yet.
struct FatalPacket
{
    uint32 Version = 1;
    std::atomic<bool> MinimalReady{false};
    usize MinimalSize = 0;
    char Minimal[512]{};
    std::atomic<bool> CompleteReady{false};
    usize CompleteSize = 0;
    char Complete[2048]{};
    std::atomic<bool> DeliveryReady{false};
    DeliveryStatus MinimalDelivery = DeliveryStatus::Unavailable;
    DeliveryStatus CompleteDelivery = DeliveryStatus::Unavailable;
};

static_assert(std::atomic<bool>::is_always_lock_free);
extern FatalPacket gFatalPacket;
} // namespace ludus::foundation::diagnostics::internal
