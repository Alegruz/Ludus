#include <ludus/foundation/profiling/clock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

using namespace ludus::foundation::profiling;
using ludus::foundation::uint64;

// Gate G3: the shared clock must be monotonic, non-decreasing, and produce
// integer-nanosecond ticks (never floating-point seconds — §5 rejects R's
// single-precision-seconds timestamps).

TEST_CASE("clock is non-decreasing on one thread", "[profiling][clock]")
{
    uint64 previous = NowTicks();
    for (int i = 0; i < 10000; ++i)
    {
        const uint64 now = NowTicks();
        REQUIRE(now >= previous);
        previous = now;
    }
}

TEST_CASE("clock advances over observable work", "[profiling][clock]")
{
    const uint64 start = NowTicks();
    // Busy-ish spin so at least one nanosecond elapses on any real clock.
    volatile uint64 sink = 0;
    for (int i = 0; i < 1'000'000; ++i)
    {
        sink = sink + static_cast<uint64>(i);
    }
    const uint64 end = NowTicks();
    CHECK(end > start);
}

TEST_CASE("ticks are nanoseconds for the steady_clock backend", "[profiling][clock]")
{
    // The MVP backend reports 1 ns per tick (ticks already are nanoseconds).
    CHECK(TicksPerNanosecondDenominator() == 1);
}

TEST_CASE("clock is non-decreasing across threads reading concurrently", "[profiling][clock]")
{
    // Not a cross-core-ordering proof (that is gate C7); this only asserts each
    // thread sees a monotonic sequence and no read faults under contention.
    constexpr int kThreads = 4;
    std::vector<std::thread> threads;
    threads.reserve(kThreads); // pre-allocate (performance-inefficient-vector-operation)
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([] {
            uint64 previous = NowTicks();
            for (int i = 0; i < 20000; ++i)
            {
                const uint64 now = NowTicks();
                REQUIRE(now >= previous);
                previous = now;
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
}
