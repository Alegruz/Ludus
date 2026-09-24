// Benchmarks: Ludus::Vector / Ludus::Array vs std::vector / std::array.
//
// Goal is NOT to manufacture a win over std::vector (a good std::vector on
// trivially-copyable types is already near-optimal). Goal: prove Ludus::Vector
// is not materially worse on representative engine workloads, and record honest
// numbers. Compiled at -O2 with the pinned Clang 18. Simple wall-clock harness;
// each workload runs enough iterations to be measurable and is repeated, taking
// the best (min) time to reduce noise.

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/vector.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using ludus::foundation::Vector;
using Clock = std::chrono::steady_clock;

namespace
{
// Prevent the optimizer from deleting work whose result is unused.
template <typename T>
inline void DoNotOptimize(const T& value)
{
    asm volatile("" : : "r,m"(value) : "memory");
}
inline void ClobberMemory()
{
    asm volatile("" : : : "memory");
}

struct Result
{
    double ludusNs;
    double stdNs;
};

template <typename Fn>
double BestNs(Fn&& fn, int reps, long innerIters)
{
    double best = 1e300;
    for (int r = 0; r < reps; ++r)
    {
        const auto t0 = Clock::now();
        fn();
        const auto t1 = Clock::now();
        const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(innerIters);
        if (ns < best)
        {
            best = ns;
        }
    }
    return best;
}

void Row(const char* name, Result r)
{
    const double ratio = r.stdNs > 0.0 ? r.ludusNs / r.stdNs : 0.0;
    std::printf("%-38s  ludus=%10.2f  std=%10.2f  ratio=%.3f%s\n", name, r.ludusNs, r.stdNs, ratio,
                ratio <= 1.05 ? "" : "   (SLOWER)");
}

// A 32-byte POD "transform-like" struct: trivially copyable => relocatable.
struct Pod32
{
    float m[8];
};

// A non-trivial element (heap-owning): exercises the move+destroy path.
struct NonTrivial
{
    std::string s;
    NonTrivial() : s("x") {}
    explicit NonTrivial(int i) : s(std::to_string(i)) {}
};
} // namespace

int main()
{
    constexpr int kReps = 7;
    constexpr long N = 100000;

    std::printf("=== Ludus::Vector vs std::vector  (Clang 18, -O2; ns per element, best of %d) ===\n", kReps);

    // --- push N uint32 (trivial scalar) ---
    {
        auto ludus = BestNs(
            [] {
                Vector<std::uint32_t> v;
                for (long i = 0; i < N; ++i)
                {
                    v.PushBack(static_cast<std::uint32_t>(i));
                }
                DoNotOptimize(v.Data());
            },
            kReps, N);
        auto stdv = BestNs(
            [] {
                std::vector<std::uint32_t> v;
                for (long i = 0; i < N; ++i)
                {
                    v.push_back(static_cast<std::uint32_t>(i));
                }
                DoNotOptimize(v.data());
            },
            kReps, N);
        Row("push N uint32 (unreserved)", {ludus, stdv});
    }

    // --- reserve + push N uint32 ---
    {
        auto ludus = BestNs(
            [] {
                Vector<std::uint32_t> v;
                v.Reserve(N);
                for (long i = 0; i < N; ++i)
                {
                    v.PushBack(static_cast<std::uint32_t>(i));
                }
                DoNotOptimize(v.Data());
            },
            kReps, N);
        auto stdv = BestNs(
            [] {
                std::vector<std::uint32_t> v;
                v.reserve(N);
                for (long i = 0; i < N; ++i)
                {
                    v.push_back(static_cast<std::uint32_t>(i));
                }
                DoNotOptimize(v.data());
            },
            kReps, N);
        Row("reserve + push N uint32", {ludus, stdv});
    }

    // --- push N POD32 (trivially relocatable) ---
    {
        auto ludus = BestNs(
            [] {
                Vector<Pod32> v;
                for (long i = 0; i < N; ++i)
                {
                    v.PushBack(Pod32{});
                }
                DoNotOptimize(v.Data());
            },
            kReps, N);
        auto stdv = BestNs(
            [] {
                std::vector<Pod32> v;
                for (long i = 0; i < N; ++i)
                {
                    v.push_back(Pod32{});
                }
                DoNotOptimize(v.data());
            },
            kReps, N);
        Row("push N POD32 (unreserved)", {ludus, stdv});
    }

    // --- push N non-trivial (move+destroy relocation path) ---
    {
        constexpr long M = 20000;
        auto ludus = BestNs(
            [] {
                Vector<NonTrivial> v;
                for (long i = 0; i < M; ++i)
                {
                    v.EmplaceBack(static_cast<int>(i));
                }
                DoNotOptimize(v.Data());
            },
            kReps, M);
        auto stdv = BestNs(
            [] {
                std::vector<NonTrivial> v;
                for (long i = 0; i < M; ++i)
                {
                    v.emplace_back(static_cast<int>(i));
                }
                DoNotOptimize(v.data());
            },
            kReps, M);
        Row("push N non-trivial (std::string elem)", {ludus, stdv});
    }

    // --- iteration / sum ---
    {
        Vector<std::uint32_t> lv;
        std::vector<std::uint32_t> sv;
        for (long i = 0; i < N; ++i)
        {
            lv.PushBack(static_cast<std::uint32_t>(i));
            sv.push_back(static_cast<std::uint32_t>(i));
        }
        auto ludus = BestNs(
            [&] {
                std::uint64_t sum = 0;
                for (auto x : lv)
                {
                    sum += x;
                }
                DoNotOptimize(sum);
            },
            kReps * 4, N);
        auto stdv = BestNs(
            [&] {
                std::uint64_t sum = 0;
                for (auto x : sv)
                {
                    sum += x;
                }
                DoNotOptimize(sum);
            },
            kReps * 4, N);
        Row("iterate + sum N uint32", {ludus, stdv});
    }

    // --- random access ---
    {
        Vector<std::uint32_t> lv;
        std::vector<std::uint32_t> sv;
        for (long i = 0; i < N; ++i)
        {
            lv.PushBack(static_cast<std::uint32_t>((i * 2654435761u) % N));
            sv.push_back(static_cast<std::uint32_t>((i * 2654435761u) % N));
        }
        auto ludus = BestNs(
            [&] {
                std::uint64_t sum = 0;
                for (long i = 0; i < N; ++i)
                {
                    sum += lv[static_cast<std::size_t>((i * 40503) % N)];
                }
                DoNotOptimize(sum);
            },
            kReps, N);
        auto stdv = BestNs(
            [&] {
                std::uint64_t sum = 0;
                for (long i = 0; i < N; ++i)
                {
                    sum += sv[static_cast<std::size_t>((i * 40503) % N)];
                }
                DoNotOptimize(sum);
            },
            kReps, N);
        Row("random access N uint32", {ludus, stdv});
    }

    // --- bulk append (span) ---
    {
        std::vector<std::uint32_t> src(N);
        for (long i = 0; i < N; ++i)
        {
            src[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(i);
        }
        auto ludus = BestNs(
            [&] {
                Vector<std::uint32_t> v;
                v.Append(std::span<const std::uint32_t>(src.data(), src.size()));
                DoNotOptimize(v.Data());
            },
            kReps, N);
        auto stdv = BestNs(
            [&] {
                std::vector<std::uint32_t> v;
                v.insert(v.end(), src.begin(), src.end());
                DoNotOptimize(v.data());
            },
            kReps, N);
        Row("bulk append N uint32", {ludus, stdv});
    }

    // --- copy whole ---
    {
        Vector<Pod32> lv;
        std::vector<Pod32> sv;
        for (long i = 0; i < N; ++i)
        {
            lv.PushBack(Pod32{});
            sv.push_back(Pod32{});
        }
        auto ludus = BestNs(
            [&] {
                Vector<Pod32> c = lv;
                DoNotOptimize(c.Data());
            },
            kReps, N);
        auto stdv = BestNs(
            [&] {
                std::vector<Pod32> c = sv;
                DoNotOptimize(c.data());
            },
            kReps, N);
        Row("copy N POD32", {ludus, stdv});
    }

    std::printf("\n=== sizeof ===\n");
    std::printf("sizeof(Ludus::Vector<int>)=%zu  sizeof(std::vector<int>)=%zu\n", sizeof(Vector<int>),
                sizeof(std::vector<int>));
    std::printf("sizeof(Ludus::Array<int,16>)=%zu  sizeof(std::array<int,16>)=%zu\n",
                sizeof(ludus::foundation::Array<int, 16>), sizeof(std::array<int, 16>));

    return 0;
}
