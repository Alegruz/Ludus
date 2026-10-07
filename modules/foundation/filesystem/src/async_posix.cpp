#include "internal/async_native.hpp"

namespace ludus::foundation::filesystem::detail
{
bool IoGate::Initialize() noexcept
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
void IoGate::Destroy() noexcept
{
    (void)pthread_cond_destroy(&Condition);
    (void)pthread_mutex_destroy(&Mutex);
}
void IoGate::Lock() noexcept
{
    LUDUS_REQUIRE(pthread_mutex_lock(&Mutex) == 0);
}
void IoGate::Unlock() noexcept
{
    LUDUS_REQUIRE(pthread_mutex_unlock(&Mutex) == 0);
}
void IoGate::Wait() noexcept
{
    LUDUS_REQUIRE(pthread_cond_wait(&Condition, &Mutex) == 0);
}
void IoGate::WakeAll() noexcept
{
    LUDUS_REQUIRE(pthread_cond_broadcast(&Condition) == 0);
}

namespace
{
void* ThreadEntry(void* context) noexcept
{
    auto& thread = *static_cast<IoWorker*>(context);
    thread.Function(thread.Context);
    return nullptr;
}
} // namespace

bool IoWorker::Start(void (*function)(void*) noexcept, void* context) noexcept
{
    Function = function;
    Context = context;
    return pthread_create(&Handle, nullptr, ThreadEntry, this) == 0;
}
void IoWorker::Join() noexcept
{
    LUDUS_REQUIRE(pthread_join(Handle, nullptr) == 0);
}
} // namespace ludus::foundation::filesystem::detail
