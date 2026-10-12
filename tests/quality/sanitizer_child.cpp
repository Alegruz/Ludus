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
    std::atomic<bool> ready{false};
    std::atomic<ludus::foundation::uint32> completed{0};
    volatile int32 value = 0;
    auto write = [&ready, &completed, &value](int32 input) {
        while (!ready.load(std::memory_order_acquire))
        {
        }
        value = input;
        // Keep both writers alive until both volatile accesses occur. Relaxed
        // atomics deliberately leave the writes unordered for the race control.
        completed.fetch_add(1, std::memory_order_relaxed);
        while (completed.load(std::memory_order_relaxed) != 2)
        {
        }
    };
    std::thread first(write, 1);
    std::thread second(write, 2);
    ready.store(true, std::memory_order_release);
    first.join();
    second.join();
    return value == 0 ? 1 : 0;
#else
#    error A sanitizer control kind is required
#endif
}
