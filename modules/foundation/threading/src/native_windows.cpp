#include "internal/native.hpp"

#include <process.h>

namespace ludus::foundation::threading::detail
{
bool NativeGate::Initialize() noexcept
{
    InitializeConditionVariable(&Condition);
    return InitializeCriticalSectionEx(&Mutex, 0, 0) != 0;
}
void NativeGate::Destroy() noexcept
{
    DeleteCriticalSection(&Mutex);
}
void NativeGate::Lock() noexcept
{
    EnterCriticalSection(&Mutex);
}
void NativeGate::Unlock() noexcept
{
    LeaveCriticalSection(&Mutex);
}
void NativeGate::Wait() noexcept
{
    LUDUS_REQUIRE(SleepConditionVariableCS(&Condition, &Mutex, INFINITE) != 0);
}
void NativeGate::WakeAll() noexcept
{
    WakeAllConditionVariable(&Condition);
}

namespace
{
uint32 __stdcall ThreadEntry(void* context) noexcept
{
    auto& thread = *static_cast<NativeThread*>(context);
    thread.Function(thread.Context);
    return 0;
}
} // namespace

bool NativeThread::Start(ThreadFunction function, void* context) noexcept
{
    Function = function;
    Context = context;
    Handle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, ThreadEntry, this, 0, nullptr));
    return Handle != nullptr;
}
void NativeThread::Join() noexcept
{
    LUDUS_REQUIRE(WaitForSingleObject(Handle, INFINITE) == WAIT_OBJECT_0);
    (void)CloseHandle(Handle);
    Handle = nullptr;
}
} // namespace ludus::foundation::threading::detail
