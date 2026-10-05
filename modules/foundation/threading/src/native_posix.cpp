#include "internal/native.hpp"

namespace ludus::foundation::threading::detail
{
bool NativeGate::Initialize() noexcept
{
    if (pthread_mutex_init(&Mutex, nullptr) != 0)
    {
        return false;
    }
    if (pthread_cond_init(&Condition, nullptr) != 0)
    {
        (void)pthread_mutex_destroy(&Mutex);
        return false;
    }
    return true;
}
void NativeGate::Destroy() noexcept
{
    (void)pthread_cond_destroy(&Condition);
    (void)pthread_mutex_destroy(&Mutex);
}
void NativeGate::Lock() noexcept
{
    LUDUS_REQUIRE(pthread_mutex_lock(&Mutex) == 0);
}
void NativeGate::Unlock() noexcept
{
    LUDUS_REQUIRE(pthread_mutex_unlock(&Mutex) == 0);
}
void NativeGate::Wait() noexcept
{
    LUDUS_REQUIRE(pthread_cond_wait(&Condition, &Mutex) == 0);
}
void NativeGate::WakeAll() noexcept
{
    LUDUS_REQUIRE(pthread_cond_broadcast(&Condition) == 0);
}

namespace
{
void* ThreadEntry(void* context) noexcept
{
    auto& thread = *static_cast<NativeThread*>(context);
    thread.Function(thread.Context);
    return nullptr;
}
} // namespace

bool NativeThread::Start(ThreadFunction function, void* context) noexcept
{
    Function = function;
    Context = context;
    return pthread_create(&Handle, nullptr, ThreadEntry, this) == 0;
}
void NativeThread::Join() noexcept
{
    LUDUS_REQUIRE(pthread_join(Handle, nullptr) == 0);
}
} // namespace ludus::foundation::threading::detail
