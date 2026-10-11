#pragma once

namespace ludus::audio::content::internal
{
// Private seam compiled only into the dedicated test backend, never the SDK.
struct WorkerTestHooks final
{
    void* Context = nullptr;
    void (*BeforeRun)(void*) noexcept = nullptr;
    void (*AfterCompletion)(void*) noexcept = nullptr;
};
// Set on the serialized test owner before Begin; copied into each acquisition.
void SetWorkerTestHooks(WorkerTestHooks hooks) noexcept;
} // namespace ludus::audio::content::internal
