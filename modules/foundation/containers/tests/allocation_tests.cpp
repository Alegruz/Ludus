// Allocation-count tests: verify Array's allocation behaviour precisely.
// We replace global operator new/delete with counting versions. This TU is built
// into its own executable (see CMakeLists) and only when sanitizers are OFF (the
// sanitizer runtimes provide their own operator new/delete).

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/static_array.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdlib>
#include <new>

namespace
{
struct AllocCounters
{
    long news = 0;
    long deletes = 0;
    std::size_t bytes = 0;
};
AllocCounters gCounters;
bool gTracking = false;

void Reset() noexcept
{
    gCounters = AllocCounters{};
}
} // namespace

// Global operator new/delete replacements routed through aligned malloc/free and
// counted when tracking is on. Signatures match the standard library exactly.
// The container allocates via the aligned nothrow new and frees via sized aligned
// delete; we count both. A helper does the aligned allocation for all overloads.
namespace
{
void* AlignedAlloc(std::size_t n, std::size_t al) noexcept
{
    if (al < alignof(std::max_align_t))
    {
        al = alignof(std::max_align_t);
    }
    std::size_t rounded = (n + al - 1) & ~(al - 1);
    if (rounded == 0)
    {
        rounded = al;
    }
    return std::aligned_alloc(al, rounded);
}
void CountNew(std::size_t n) noexcept
{
    if (gTracking)
    {
        ++gCounters.news;
        gCounters.bytes += n;
    }
}
void CountDelete(void* p) noexcept
{
    if (p != nullptr && gTracking)
    {
        ++gCounters.deletes;
    }
}
} // namespace

void* operator new(std::size_t n)
{
    CountNew(n);
    void* p = AlignedAlloc(n, alignof(std::max_align_t));
    if (p == nullptr)
    {
        throw std::bad_alloc{};
    }
    return p;
}
void* operator new(std::size_t n, std::align_val_t al)
{
    CountNew(n);
    void* p = AlignedAlloc(n, static_cast<std::size_t>(al));
    if (p == nullptr)
    {
        throw std::bad_alloc{};
    }
    return p;
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept
{
    CountNew(n);
    return AlignedAlloc(n, alignof(std::max_align_t));
}
void* operator new(std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept
{
    CountNew(n);
    return AlignedAlloc(n, static_cast<std::size_t>(al));
}
void operator delete(void* p) noexcept
{
    CountDelete(p);
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    CountDelete(p);
    std::free(p);
}
void operator delete(void* p, std::align_val_t) noexcept
{
    CountDelete(p);
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    CountDelete(p);
    std::free(p);
}

using ludus::foundation::Array;
using ludus::foundation::StaticArray;

namespace
{
struct TrackGuard
{
    TrackGuard()
    {
        Reset();
        gTracking = true;
    }
    ~TrackGuard()
    {
        gTracking = false;
    }
};
} // namespace

TEST_CASE("Default-constructed Array never allocates", "[alloc]")
{
    TrackGuard g;
    {
        Array<int> v;
        (void)v.IsEmpty();
    }
    REQUIRE(gCounters.news == 0);
    REQUIRE(gCounters.deletes == 0);
}

TEST_CASE("StaticArray never allocates", "[alloc]")
{
    TrackGuard g;
    {
        StaticArray<int, 32> a{};
        a.Fill(7);
        volatile int sink = a[0];
        (void)sink;
    }
    REQUIRE(gCounters.news == 0);
}

TEST_CASE("EnsureCapacity(n) + n Adds allocates exactly once", "[alloc]")
{
    TrackGuard g;
    {
        Array<int> v;
        v.EnsureCapacity(256);
        for (int i = 0; i < 256; ++i)
        {
            v.Add(i);
        }
        REQUIRE(gCounters.news == 1);
    }
    REQUIRE(gCounters.news == 1);
    REQUIRE(gCounters.deletes == 1); // symmetric free on destruction
}

TEST_CASE("Clear does not free or allocate", "[alloc]")
{
    Array<int> v;
    v.EnsureCapacity(64);
    for (int i = 0; i < 64; ++i)
    {
        v.Add(i);
    }
    {
        TrackGuard g;
        v.Clear();
        REQUIRE(gCounters.news == 0);
        REQUIRE(gCounters.deletes == 0);
    }
    REQUIRE(v.GetCapacity() >= 64);
}

TEST_CASE("Growth reallocates a bounded number of times", "[alloc]")
{
    TrackGuard g;
    {
        Array<int> v;
        for (int i = 0; i < 10000; ++i)
        {
            v.Add(i);
        }
        // Geometric growth => O(log n) reallocations, far fewer than n.
        REQUIRE(gCounters.news < 40);
        REQUIRE(gCounters.news > 1);
    }
}

TEST_CASE("Allocation/deallocation symmetry after churn", "[alloc]")
{
    TrackGuard g;
    {
        Array<int> v;
        for (int i = 0; i < 1000; ++i)
        {
            v.Add(i);
        }
        v.TrimCapacity();
        Array<int> moved = std::move(v);
        moved.Clear();
        moved.TrimCapacity();
    }
    REQUIRE(gCounters.news == gCounters.deletes);
}
