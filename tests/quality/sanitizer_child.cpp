#include <ludus/foundation/base/types.h>

#if defined(LUDUS_CONTROL_TSAN)
#    include <atomic>
#    include <thread>
#endif

// Deliberate test-only defects. Run through sanitizer_control.py, which checks
// both a clean startup and the specific fatal diagnostic in separate processes.
// Thanks to LLVM, "UndefinedBehaviorSanitizer", Clang 18.1.8, Usage, for the
// signed-overflow control; adapted to Ludus types and volatile input/output.
// https://releases.llvm.org/18.1.8/tools/clang/docs/UndefinedBehaviorSanitizer.html
int main(int argumentCount, char**)
{
    using ludus::foundation::int32;
    if (argumentCount == 1)
    {
        return 0;
    }

#if defined(LUDUS_CONTROL_ASAN)
    volatile int32 index = argumentCount - 1;
    volatile int32* values = new int32[1];
    values[index] = 7;
    volatile int32 observed = values[index];
    delete[] values;
    return observed == 7 ? 0 : 1;
#elif defined(LUDUS_CONTROL_UBSAN)
    volatile int32 value = 0x7fffffff;
    value = value + argumentCount;
    return value == 0 ? 1 : 0;
#elif defined(LUDUS_CONTROL_TSAN)
    // Thanks to LLVM, "ThreadSanitizer", Clang 18.1.8, Usage (tiny_race.c),
    // for conflicting parent/worker writes before join. Adapted to std::thread,
    // a pre-write start gate and an isolated volatile slot; no post-write rendezvous.
    // https://releases.llvm.org/18.1.8/tools/clang/docs/ThreadSanitizer.html
    std::atomic<bool> ready{false};
    alignas(64) volatile int32 value = 0;
    std::thread worker([&ready, &value] {
        while (!ready.load(std::memory_order_acquire))
        {
        }
        value = 1;
    });
    // Both writes follow the release; neither write happens-before the other.
    // Keep atomics on separate storage and all synchronization before the defect.
    ready.store(true, std::memory_order_release);
    value = 2;
    worker.join();
    return value == 0 ? 1 : 0;
#else
#    error A sanitizer control kind is required
#endif
}
