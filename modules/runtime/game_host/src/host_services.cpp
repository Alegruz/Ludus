#include "internal/host_services.h"

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>
#include <string_view>
#include <unordered_map>

namespace ludus::runtime::game_host
{
namespace
{
LUDUS_DEFINE_LOG_CATEGORY(LOG_GAME_HOST, "GameHost");

using game_api::HostContext;
using game_api::HostServices;
using game_api::LogSeverity;

// Bounded allocation cap per session so a misbehaving module cannot exhaust the
// host. 64 MiB is generous for the supported fixtures (design 6: bounded).
constexpr usize kMaxAllocationBytes = static_cast<usize>(64) * 1024 * 1024;

// Header stored just before each allocation so FreeBytes can account exactly.
struct alignas(std::max_align_t) AllocationHeader
{
    usize Size = 0;
    void* Base = nullptr; // Original aligned_alloc pointer.
};
} // namespace

struct HostServiceProvider::Context
{
    std::atomic<uint64> Outstanding{0};
    std::atomic<usize> BytesLive{0};
    std::unordered_map<uint64, uint64> Resources;
};

namespace
{
// The HostContext* handed over the ABI is a reinterpret of our Context. It is
// opaque to the module; only our service callbacks interpret it.
[[nodiscard]] HostServiceProvider::Context* Decode(HostContext* ctx) noexcept
{
    return reinterpret_cast<HostServiceProvider::Context*>(ctx);
}

void LogCallback(HostContext* ctx, LogSeverity severity, const char* text, usize length) noexcept
{
    (void)ctx;
    if (text == nullptr || length == 0 || length > 4096)
    {
        return;
    }
    const std::string_view message(text, length);
    switch (severity)
    {
        case LogSeverity::Trace:
            LUDUS_LOG_TRACE(LOG_GAME_HOST, "{}", message);
            break;
        case LogSeverity::Debug:
            LUDUS_LOG_DEBUG(LOG_GAME_HOST, "{}", message);
            break;
        case LogSeverity::Info:
            LUDUS_LOG_INFO(LOG_GAME_HOST, "{}", message);
            break;
        case LogSeverity::Warning:
            LUDUS_LOG_WARN(LOG_GAME_HOST, "{}", message);
            break;
        case LogSeverity::Error:
            LUDUS_LOG_ERROR(LOG_GAME_HOST, "{}", message);
            break;
    }
}

// The size/alignment parameter order is the fixed ABI service signature.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void* AllocateCallback(HostContext* ctx, usize size, usize alignment) noexcept
{
    auto* context = Decode(ctx);
    if (context == nullptr || size == 0 || size > kMaxAllocationBytes)
    {
        return nullptr;
    }
    if (alignment < alignof(std::max_align_t))
    {
        alignment = alignof(std::max_align_t);
    }
    // Round up to a power-of-two alignment multiple for aligned_alloc.
    if ((alignment & (alignment - 1)) != 0)
    {
        return nullptr;
    }
    const usize prior = context->BytesLive.load(std::memory_order_relaxed);
    if (prior + size > kMaxAllocationBytes)
    {
        return nullptr;
    }

    // Over-allocate room for the header plus worst-case alignment slack, then
    // place the user block with std::align (no integer<->pointer casts).
    void* base = std::malloc(size + sizeof(AllocationHeader) + alignment);
    if (base == nullptr)
    {
        return nullptr;
    }
    void* cursor = static_cast<char*>(base) + sizeof(AllocationHeader);
    usize space = size + alignment;
    void* user = std::align(alignment, size, cursor, space);
    if (user == nullptr)
    {
        std::free(base);
        return nullptr;
    }
    auto* header = reinterpret_cast<AllocationHeader*>(static_cast<char*>(user) - sizeof(AllocationHeader));
    header->Size = size;
    header->Base = base;

    context->BytesLive.fetch_add(size, std::memory_order_relaxed);
    context->Outstanding.fetch_add(1, std::memory_order_relaxed);
    return user;
}

void FreeCallback(HostContext* ctx, void* ptr) noexcept
{
    auto* context = Decode(ctx);
    if (context == nullptr || ptr == nullptr)
    {
        return;
    }
    auto* header = reinterpret_cast<AllocationHeader*>(static_cast<char*>(ptr) - sizeof(AllocationHeader));
    const usize size = header->Size;
    void* base = header->Base;
    std::free(base);
    context->BytesLive.fetch_sub(size, std::memory_order_relaxed);
    context->Outstanding.fetch_sub(1, std::memory_order_relaxed);
}

uint64 ResolveResourceCallback(HostContext* ctx, uint64 logicalAssetId) noexcept
{
    auto* context = Decode(ctx);
    if (context == nullptr)
    {
        return 0;
    }
    const auto it = context->Resources.find(logicalAssetId);
    return it == context->Resources.end() ? 0 : it->second;
}
} // namespace

HostServiceProvider::HostServiceProvider() noexcept : Context_(new(std::nothrow) Context())
{
    Services_.StructSize = static_cast<ludus::foundation::uint32>(sizeof(HostServices));
    Services_.Context = reinterpret_cast<HostContext*>(Context_);
    Services_.Log = &LogCallback;
    Services_.AllocateBytes = &AllocateCallback;
    Services_.FreeBytes = &FreeCallback;
    Services_.ResolveResource = &ResolveResourceCallback;
}

HostServiceProvider::~HostServiceProvider() noexcept
{
    delete Context_;
    Context_ = nullptr;
    Services_ = {};
}

uint64 HostServiceProvider::OutstandingAllocations() const noexcept
{
    return Context_ == nullptr ? 0 : Context_->Outstanding.load(std::memory_order_relaxed);
}

void HostServiceProvider::SetResource(uint64 logicalAssetId, uint64 resourceHandle) noexcept
{
    if (Context_ != nullptr)
    {
        Context_->Resources[logicalAssetId] = resourceHandle;
    }
}
} // namespace ludus::runtime::game_host
