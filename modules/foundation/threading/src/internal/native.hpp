#pragma once

#include <ludus/foundation/base/core.h>

#if defined(LUDUS_PLATFORM_WINDOWS)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#elif !defined(LUDUS_PLATFORM_WEB)
#    include <pthread.h>
#endif

namespace ludus::foundation::threading::detail
{
// Private OS boundary: thread creation reports failure instead of using the
// throwing std::thread constructor. Gate initialization is fallible as well.
struct NativeGate
{
#if defined(LUDUS_PLATFORM_WINDOWS)
    CRITICAL_SECTION Mutex{};
    CONDITION_VARIABLE Condition{};
#elif !defined(LUDUS_PLATFORM_WEB)
    pthread_mutex_t Mutex{};
    pthread_cond_t Condition{};
#endif
    [[nodiscard]] bool Initialize() noexcept;
    void Destroy() noexcept;
    void Lock() noexcept;
    void Unlock() noexcept;
    void Wait() noexcept;
    void WakeAll() noexcept;
};

using ThreadFunction = void (*)(void*) noexcept;
struct NativeThread
{
#if defined(LUDUS_PLATFORM_WINDOWS)
    HANDLE Handle = nullptr;
#elif !defined(LUDUS_PLATFORM_WEB)
    pthread_t Handle{};
#endif
    ThreadFunction Function = nullptr;
    void* Context = nullptr;
    [[nodiscard]] bool Start(ThreadFunction function, void* context) noexcept;
    void Join() noexcept;
};

class GateLock final
{
public:
    explicit GateLock(NativeGate& gate) noexcept : mGate(gate)
    {
        mGate.Lock();
    }
    ~GateLock() noexcept
    {
        mGate.Unlock();
    }
    GateLock(const GateLock&) = delete;
    GateLock& operator=(const GateLock&) = delete;

private:
    NativeGate& mGate;
};
} // namespace ludus::foundation::threading::detail
