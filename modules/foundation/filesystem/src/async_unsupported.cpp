#include "internal/async_native.hpp"

namespace ludus::foundation::filesystem::detail
{
bool IoGate::Initialize() noexcept
{
    return true;
}
void IoGate::Destroy() noexcept {}
void IoGate::Lock() noexcept {}
void IoGate::Unlock() noexcept {}
void IoGate::Wait() noexcept
{
    LUDUS_FATAL("Serial executor cannot sleep waiting for work");
}
void IoGate::WakeAll() noexcept {}
bool IoWorker::Start(void (*)(void*) noexcept, void*) noexcept
{
    return false;
}
void IoWorker::Join() noexcept {}
} // namespace ludus::foundation::filesystem::detail
