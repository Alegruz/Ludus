// Unit tests for Ludus::Vector<T>. Validates lifetime (not just final values),
// growth, capacity, modifiers, copy/move, self-assignment, insert/erase,
// aliasing, and std interop across trivial / non-trivial / move-only /
// over-aligned / empty element types.

#include "lifetime_type.hpp"

#include <ludus/foundation/containers/vector.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numeric>
#include <ranges>
#include <span>
#include <type_traits>

using ludus::foundation::usize;
using ludus::foundation::Vector;
namespace tst = ludus::containers::testing;

TEST_CASE("Vector empty representation", "[vector]")
{
    Vector<int> v;
    REQUIRE(v.Size() == 0);
    REQUIRE(v.Capacity() == 0);
    REQUIRE(v.Empty());
    REQUIRE(v.Data() == nullptr);
    REQUIRE(v.begin() == v.end());
    REQUIRE(sizeof(Vector<int>) == 3 * sizeof(void*)); // {ptr,size,capacity}
}

TEST_CASE("Vector push_back grows and preserves order", "[vector]")
{
    Vector<int> v;
    for (int i = 0; i < 1000; ++i)
    {
        v.PushBack(i);
    }
    REQUIRE(v.Size() == 1000);
    REQUIRE(v.Capacity() >= 1000);
    for (int i = 0; i < 1000; ++i)
    {
        REQUIRE(v[static_cast<usize>(i)] == i);
    }
    REQUIRE(v.Front() == 0);
    REQUIRE(v.Back() == 999);
}

TEST_CASE("Vector size <= capacity invariant across growth", "[vector]")
{
    Vector<int> v;
    for (int i = 0; i < 500; ++i)
    {
        v.PushBack(i);
        REQUIRE(v.Size() <= v.Capacity());
    }
}

TEST_CASE("Vector lifetime ledger balances (non-trivial type)", "[vector][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Vector<tst::Tracked> v;
        for (int i = 0; i < 200; ++i)
        {
            v.EmplaceBack(&led, i);
        }
        REQUIRE(v.Size() == 200);
        // Values survived every reallocation.
        for (int i = 0; i < 200; ++i)
        {
            REQUIRE(v[static_cast<usize>(i)].Value() == i);
        }
    }
    REQUIRE(led.Balanced());
    REQUIRE(led.Live() == 0);
}

TEST_CASE("Vector clear destroys elements, keeps capacity", "[vector][lifetime]")
{
    tst::LifetimeLedger led;
    Vector<tst::Tracked> v;
    v.Reserve(16); // no reallocation during the pushes => exact dtor accounting
    for (int i = 0; i < 10; ++i)
    {
        v.EmplaceBack(&led, i);
    }
    const usize cap = v.Capacity();
    const long liveBeforeClear = led.Live();
    REQUIRE(liveBeforeClear == 10);
    v.Clear();
    REQUIRE(v.Size() == 0);
    REQUIRE(v.Capacity() == cap); // capacity retained
    REQUIRE(led.Live() == 0);     // Clear destroyed exactly the 10 live elements
}

TEST_CASE("Vector reserve allocates once and preserves elements", "[vector]")
{
    Vector<int> v;
    v.Reserve(128);
    REQUIRE(v.Capacity() >= 128);
    const int* base = v.Data();
    for (int i = 0; i < 128; ++i)
    {
        v.PushBack(i);
    }
    REQUIRE(v.Data() == base); // no reallocation within reserved capacity
    REQUIRE(v.Size() == 128);
}

TEST_CASE("Vector resize up and down", "[vector][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Vector<tst::Tracked> v;
        v.Reserve(4);
        v.EmplaceBack(&led, 1);
        v.EmplaceBack(&led, 2);
        // Resize down destroys the tail.
        v.Resize(1);
        REQUIRE(v.Size() == 1);
        REQUIRE(v[0].Value() == 1);
    }
    REQUIRE(led.Live() == 0);

    Vector<int> vi;
    vi.Resize(5);
    REQUIRE(vi.Size() == 5);
    for (int x : vi)
    {
        REQUIRE(x == 0); // value-initialized
    }
    vi.Resize(8, 7);
    REQUIRE(vi.Size() == 8);
    REQUIRE(vi[7] == 7);
    REQUIRE(vi[4] == 0);
}

TEST_CASE("Vector count/fill constructors", "[vector]")
{
    Vector<int> a(5);
    REQUIRE(a.Size() == 5);
    for (int x : a)
    {
        REQUIRE(x == 0);
    }
    Vector<int> b(3, 9);
    REQUIRE(b.Size() == 3);
    REQUIRE(b[0] == 9);
    REQUIRE(b[2] == 9);
}

TEST_CASE("Vector copy is deep and independent", "[vector][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Vector<tst::Tracked> a;
        for (int i = 0; i < 5; ++i)
        {
            a.EmplaceBack(&led, i);
        }
        Vector<tst::Tracked> b = a;
        REQUIRE(b.Size() == 5);
        b[0].SetValue(999);
        REQUIRE(a[0].Value() == 0); // independent storage
        REQUIRE(b[0].Value() == 999);
    }
    REQUIRE(led.Live() == 0);
}

TEST_CASE("Vector move leaves source empty and valid", "[vector][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Vector<tst::Tracked> a;
        for (int i = 0; i < 5; ++i)
        {
            a.EmplaceBack(&led, i);
        }
        const long ctorsBefore = led.Constructions();
        Vector<tst::Tracked> b = std::move(a);
        REQUIRE(led.Constructions() == ctorsBefore); // move steals; no element moves
        REQUIRE(b.Size() == 5);
        REQUIRE(a.Size() == 0); // NOLINT: intentional moved-from inspection
        REQUIRE(a.Capacity() == 0);
        REQUIRE(a.Data() == nullptr);
        a.PushBack(tst::Tracked(&led, 42)); // reuse moved-from
        REQUIRE(a.Size() == 1);
    }
    REQUIRE(led.Live() == 0);
}

TEST_CASE("Vector self copy- and move-assignment are safe", "[vector]")
{
    Vector<int> v;
    for (int i = 0; i < 10; ++i)
    {
        v.PushBack(i);
    }
#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wself-assign-overloaded"
#    pragma clang diagnostic ignored "-Wself-move"
#endif
    v = v; // NOLINT: exercising the self-copy-assign guard on purpose
    REQUIRE(v.Size() == 10);
    REQUIRE(v[5] == 5);
    v = std::move(v); // NOLINT: exercising the self-move-assign guard on purpose
#if defined(__clang__)
#    pragma clang diagnostic pop
#endif
    REQUIRE(v.Size() == 10);
    REQUIRE(v[9] == 9);
}

TEST_CASE("Vector move-only element type (UniquePtr-like)", "[vector][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Vector<tst::MoveOnly> v;
        for (int i = 0; i < 50; ++i)
        {
            v.EmplaceBack(&led, i);
        }
        REQUIRE(v.Size() == 50);
        REQUIRE(v[10].Value() == 10);
        Vector<tst::MoveOnly> moved = std::move(v);
        REQUIRE(moved.Size() == 50);
    }
    REQUIRE(led.Live() == 0);
}

TEST_CASE("Vector pop_back", "[vector][lifetime]")
{
    tst::LifetimeLedger led;
    Vector<tst::Tracked> v;
    v.EmplaceBack(&led, 1);
    v.EmplaceBack(&led, 2);
    v.PopBack();
    REQUIRE(v.Size() == 1);
    REQUIRE(v.Back().Value() == 1);
    v.PopBack();
    REQUIRE(v.Empty());
    REQUIRE(led.Live() == 0);
}

TEST_CASE("Vector insert/erase ordered", "[vector]")
{
    Vector<int> v;
    for (int i = 0; i < 5; ++i)
    {
        v.PushBack(i * 10); // 0 10 20 30 40
    }
    v.Insert(2, 99); // 0 10 99 20 30 40
    REQUIRE(v.Size() == 6);
    REQUIRE(v[2] == 99);
    REQUIRE(v[3] == 20);
    REQUIRE(v[5] == 40);

    v.Erase(2); // back to 0 10 20 30 40
    REQUIRE(v.Size() == 5);
    REQUIRE(v[2] == 20);

    v.Insert(0, -1); // -1 0 10 20 30 40
    REQUIRE(v.Front() == -1);
    v.Insert(v.Size(), 100); // append via insert-at-end
    REQUIRE(v.Back() == 100);
}

TEST_CASE("Vector insert/erase with lifetime tracking", "[vector][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Vector<tst::Tracked> v;
        for (int i = 0; i < 6; ++i)
        {
            v.EmplaceBack(&led, i);
        }
        v.EmplaceAt(3, &led, 100);
        REQUIRE(v[3].Value() == 100);
        REQUIRE(v[4].Value() == 3);
        v.Erase(0);
        REQUIRE(v[0].Value() == 1);
        v.EraseRange(1, 3);
        REQUIRE(v.Size() == 4);
    }
    REQUIRE(led.Live() == 0);
}

TEST_CASE("Vector erase range full and partial", "[vector]")
{
    Vector<int> v;
    for (int i = 0; i < 10; ++i)
    {
        v.PushBack(i);
    }
    v.EraseRange(2, 5); // remove 2,3,4
    REQUIRE(v.Size() == 7);
    REQUIRE(v[2] == 5);
    v.EraseRange(0, v.Size()); // clear via range
    REQUIRE(v.Empty());
}

TEST_CASE("Vector EraseUnordered is O(1) swap-with-last", "[vector]")
{
    Vector<int> v;
    for (int i = 0; i < 5; ++i)
    {
        v.PushBack(i); // 0 1 2 3 4
    }
    v.EraseUnordered(1); // last (4) moves into slot 1 -> 0 4 2 3
    REQUIRE(v.Size() == 4);
    REQUIRE(v[1] == 4);
    v.EraseUnordered(3); // erase last element directly
    REQUIRE(v.Size() == 3);
    REQUIRE(v[0] == 0);
}

TEST_CASE("Vector append from span (bulk)", "[vector][interop]")
{
    Vector<int> v;
    int src[] = {1, 2, 3, 4, 5};
    v.Append(std::span<const int>(src, 5));
    REQUIRE(v.Size() == 5);
    v.Append(std::span<const int>(src, 5));
    REQUIRE(v.Size() == 10);
    REQUIRE(v[9] == 5);
    // Append empty is a no-op.
    v.Append(std::span<const int>());
    REQUIRE(v.Size() == 10);
}

TEST_CASE("Vector From factory", "[vector]")
{
    int src[] = {9, 8, 7};
    Vector<int> v = Vector<int>::From(std::span<const int>(src, 3));
    REQUIRE(v.Size() == 3);
    REQUIRE(v[0] == 9);
    REQUIRE(v[2] == 7);
}

TEST_CASE("Vector swap", "[vector]")
{
    Vector<int> a;
    a.PushBack(1);
    a.PushBack(2);
    Vector<int> b;
    b.PushBack(9);
    a.Swap(b);
    REQUIRE(a.Size() == 1);
    REQUIRE(a[0] == 9);
    REQUIRE(b.Size() == 2);
    REQUIRE(b[1] == 2);
}

TEST_CASE("Vector shrink_to_fit", "[vector]")
{
    Vector<int> v;
    v.Reserve(100);
    v.PushBack(1);
    v.PushBack(2);
    REQUIRE(v.Capacity() >= 100);
    v.ShrinkToFit();
    REQUIRE(v.Capacity() == 2);
    REQUIRE(v.Size() == 2);
    REQUIRE(v[0] == 1);
    v.Clear();
    v.ShrinkToFit();
    REQUIRE(v.Capacity() == 0);
    REQUIRE(v.Data() == nullptr);
}

TEST_CASE("Vector std interop: ranges, algorithms, span, pointer+count", "[vector][interop]")
{
    Vector<int> v;
    for (int i = 5; i >= 1; --i)
    {
        v.PushBack(i);
    }
    std::sort(v.begin(), v.end());
    REQUIRE(v[0] == 1);
    REQUIRE(std::is_sorted(v.begin(), v.end()));
    REQUIRE(std::accumulate(v.begin(), v.end(), 0) == 15);
    REQUIRE(std::ranges::find(v, 3) != v.end());

    // range-for
    int sum = 0;
    for (int x : v)
    {
        sum += x;
    }
    REQUIRE(sum == 15);

    // std::span conversion (GPU/C pointer+count idiom)
    std::span<const int> s = v;
    REQUIRE(s.size() == 5);
    REQUIRE(s.data() == v.Data());
}

TEST_CASE("Vector over-aligned elements", "[vector][alignment]")
{
    Vector<tst::OverAligned> v;
    for (int i = 0; i < 20; ++i)
    {
        v.EmplaceBack(i);
    }
    REQUIRE(reinterpret_cast<std::uintptr_t>(v.Data()) % 128 == 0);
    REQUIRE(v[19].Value == 19);
}

TEST_CASE("Vector empty element type", "[vector]")
{
    Vector<tst::Empty> v;
    for (int i = 0; i < 10; ++i)
    {
        v.EmplaceBack();
    }
    REQUIRE(v.Size() == 10);
    v.PopBack();
    REQUIRE(v.Size() == 9);
}

TEST_CASE("Vector equality", "[vector]")
{
    Vector<int> a;
    Vector<int> b;
    a.PushBack(1);
    a.PushBack(2);
    b.PushBack(1);
    b.PushBack(2);
    REQUIRE(a == b);
    b.PushBack(3);
    REQUIRE_FALSE(a == b);
}

TEST_CASE("Vector zero and one element edge cases", "[vector]")
{
    Vector<int> v;
    REQUIRE(v.TryReserve(0));
    REQUIRE(v.Capacity() == 0);
    v.PushBack(42);
    REQUIRE(v.Size() == 1);
    REQUIRE(v.Front() == v.Back());
    v.PopBack();
    REQUIRE(v.Empty());
}

TEST_CASE("Vector MaxSize overflow defense (fallible)", "[vector][overflow]")
{
    Vector<int> v;
    // A request beyond MaxSize must fail gracefully, not wrap/allocate tiny.
    REQUIRE_FALSE(v.TryReserve(Vector<int>::MaxSize() + 1));
    REQUIRE(v.Size() == 0);
    REQUIRE(v.Capacity() == 0);
}

// ---------------------------------------------------------------------------
// Regression tests for the self-referential insertion bug found in audit
// (heap-use-after-free / silent wrong value when an argument aliased an element
// and the operation reallocated or shifted). std::vector guarantees these are
// well-defined; Ludus::Vector must match.
// ---------------------------------------------------------------------------

TEST_CASE("Vector self-referential PushBack triggering reallocation", "[vector][regression][aliasing]")
{
    Vector<int> v;
    v.Reserve(4);
    v.PushBack(10);
    v.PushBack(20);
    v.PushBack(30);
    v.PushBack(40);   // size == capacity == 4
    v.PushBack(v[0]); // arg aliases storage that reallocation frees
    REQUIRE(v.Size() == 5);
    REQUIRE(v[4] == 10);
    // Also v.Back() self-reference across several growths.
    for (int i = 0; i < 100; ++i)
    {
        v.PushBack(v.Back());
    }
    REQUIRE(v.Back() == 10);
}

TEST_CASE("Vector self-referential EmplaceBack triggering reallocation", "[vector][regression][aliasing]")
{
    Vector<int> v;
    v.Reserve(2);
    v.PushBack(7);
    v.PushBack(8);       // full
    v.EmplaceBack(v[1]); // realloc happens; arg aliases v[1]
    REQUIRE(v.Size() == 3);
    REQUIRE(v[2] == 8);
}

TEST_CASE("Vector self-referential EmplaceAt without reallocation", "[vector][regression][aliasing]")
{
    Vector<int> v;
    for (int i = 0; i < 6; ++i)
    {
        v.PushBack(i * 10); // 0 10 20 30 40 50
    }
    const int expected = v[5]; // 50
    v.EmplaceAt(1, v[5]);      // v[5] is shifted during the op
    REQUIRE(v.Size() == 7);
    REQUIRE(v[1] == expected);
    REQUIRE(v[2] == 10); // original v[1] shifted right
}

TEST_CASE("Vector self-referential EmplaceAt triggering reallocation", "[vector][regression][aliasing]")
{
    Vector<int> v;
    v.Reserve(4);
    v.PushBack(1);
    v.PushBack(2);
    v.PushBack(3);
    v.PushBack(4); // full
    const int expected = v[3];
    v.EmplaceAt(1, v[3]); // realloc + insert of a self-reference
    REQUIRE(v.Size() == 5);
    REQUIRE(v[1] == expected);
}

TEST_CASE("Vector self-referential Insert of a shifted element", "[vector][regression][aliasing]")
{
    Vector<int> v;
    v.Reserve(8);
    for (int i = 0; i < 5; ++i)
    {
        v.PushBack(i); // 0 1 2 3 4
    }
    v.Insert(1, v[2]); // v[2] (==2) is shifted right during Insert
    REQUIRE(v[1] == 2);
}

TEST_CASE("Vector self-referential PushBack of non-trivial type across growth",
          "[vector][regression][aliasing][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Vector<tst::Tracked> v;
        v.Reserve(2);
        v.EmplaceBack(&led, 1);
        v.EmplaceBack(&led, 2); // full
        // Self-referential copy of an existing element while reallocating. The
        // Tracked canary + ASan would catch a use-after-free of the old block.
        v.PushBack(v[0]);
        REQUIRE(v.Size() == 3);
        REQUIRE(v[2].Value() == 1);
        // Force several more growths with self-references.
        for (int i = 0; i < 50; ++i)
        {
            v.PushBack(v.Back());
        }
        REQUIRE(v.Back().Value() == 1);
    }
    REQUIRE(led.Live() == 0); // no leak / no double-destroy
}

TEST_CASE("Vector self-referential EmplaceBack of move-eligible relocatable across growth",
          "[vector][regression][aliasing]")
{
    // Relocatable opts into memcpy relocation; the new element must still be
    // constructed from the aliased source before the old block is freed.
    tst::LifetimeLedger led;
    Vector<tst::Relocatable> v;
    v.Reserve(2);
    v.EmplaceBack(&led, 100);
    v.EmplaceBack(&led, 200); // full
    v.PushBack(v[1]);         // copy of v[1] while reallocating (memcpy path)
    REQUIRE(v.Size() == 3);
    REQUIRE(v[2].Value == 200);
}
