#include "internal/native.hpp"

namespace ludus::foundation::threading::detail
{
bool NativeGate::Initialize() noexcept
{
    return true;
}
void NativeGate::Destroy() noexcept {}
void NativeGate::Lock() noexcept {}
void NativeGate::Unlock() noexcept {}
void NativeGate::Wait() noexcept
{
    LUDUS_FATAL("Serial executor cannot sleep waiting for work");
}
void NativeGate::WakeAll() noexcept {}
bool NativeThread::Start(ThreadFunction, void*) noexcept
{
    return false;
}
void NativeThread::Join() noexcept {}
} // namespace ludus::foundation::threading::detail
