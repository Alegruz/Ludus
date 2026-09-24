// Unit tests for Ludus::Array<T>. Validates lifetime (not just final values),
// growth, capacity, modifiers, copy/move, self-assignment, insert/erase,
// aliasing, and std interop across trivial / non-trivial / move-only /
// over-aligned / empty element types.

#include "lifetime_type.hpp"

#include <ludus/foundation/containers/array.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numeric>
#include <ranges>
#include <span>
#include <type_traits>

using ludus::foundation::Array;
using ludus::foundation::usize;
namespace tst = ludus::containers::testing;

TEST_CASE("Array empty representation", "[array]")
{
    Array<int> v;
    REQUIRE(v.GetSize() == 0);
    REQUIRE(v.GetCapacity() == 0);
    REQUIRE(v.IsEmpty());
    REQUIRE(v.GetData() == nullptr);
    REQUIRE(v.begin() == v.end());
    REQUIRE(sizeof(Array<int>) == 3 * sizeof(void*)); // {ptr,size,capacity}
}

TEST_CASE("Array push_back grows and preserves order", "[array]")
{
    Array<int> v;
    for (int i = 0; i < 1000; ++i)
    {
        v.Add(i);
    }
    REQUIRE(v.GetSize() == 1000);
    REQUIRE(v.GetCapacity() >= 1000);
    for (int i = 0; i < 1000; ++i)
    {
        REQUIRE(v[static_cast<usize>(i)] == i);
    }
    REQUIRE(v.GetFirst() == 0);
    REQUIRE(v.GetLast() == 999);
}

TEST_CASE("Array size <= capacity invariant across growth", "[array]")
{
    Array<int> v;
    for (int i = 0; i < 500; ++i)
    {
        v.Add(i);
        REQUIRE(v.GetSize() <= v.GetCapacity());
    }
}

TEST_CASE("Array lifetime ledger balances (non-trivial type)", "[array][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Array<tst::Tracked> v;
        for (int i = 0; i < 200; ++i)
        {
            v.AddInPlace(&led, i);
        }
        REQUIRE(v.GetSize() == 200);
        // Values survived every reallocation.
        for (int i = 0; i < 200; ++i)
        {
            REQUIRE(v[static_cast<usize>(i)].Value() == i);
        }
    }
    REQUIRE(led.Balanced());
    REQUIRE(led.Live() == 0);
}

TEST_CASE("Array clear destroys elements, keeps capacity", "[array][lifetime]")
{
    tst::LifetimeLedger led;
    Array<tst::Tracked> v;
    v.EnsureCapacity(16); // no reallocation during the pushes => exact dtor accounting
    for (int i = 0; i < 10; ++i)
    {
        v.AddInPlace(&led, i);
    }
    const usize cap = v.GetCapacity();
    const long liveBeforeClear = led.Live();
    REQUIRE(liveBeforeClear == 10);
    v.Clear();
    REQUIRE(v.GetSize() == 0);
    REQUIRE(v.GetCapacity() == cap); // capacity retained
    REQUIRE(led.Live() == 0);        // Clear destroyed exactly the 10 live elements
}

TEST_CASE("Array reserve allocates once and preserves elements", "[array]")
{
    Array<int> v;
    v.EnsureCapacity(128);
    REQUIRE(v.GetCapacity() >= 128);
    const int* base = v.GetData();
    for (int i = 0; i < 128; ++i)
    {
        v.Add(i);
    }
    REQUIRE(v.GetData() == base); // no reallocation within reserved capacity
    REQUIRE(v.GetSize() == 128);
}

TEST_CASE("Array resize up and down", "[array][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Array<tst::Tracked> v;
        v.EnsureCapacity(4);
        v.AddInPlace(&led, 1);
        v.AddInPlace(&led, 2);
        // Resize down destroys the tail.
        v.Resize(1);
        REQUIRE(v.GetSize() == 1);
        REQUIRE(v[0].Value() == 1);
    }
    REQUIRE(led.Live() == 0);

    Array<int> vi;
    vi.Resize(5);
    REQUIRE(vi.GetSize() == 5);
    for (int x : vi)
    {
        REQUIRE(x == 0); // value-initialized
    }
    vi.Resize(8, 7);
    REQUIRE(vi.GetSize() == 8);
    REQUIRE(vi[7] == 7);
    REQUIRE(vi[4] == 0);
}

TEST_CASE("Array count/fill constructors", "[array]")
{
    Array<int> a(5);
    REQUIRE(a.GetSize() == 5);
    for (int x : a)
    {
        REQUIRE(x == 0);
    }
    Array<int> b(3, 9);
    REQUIRE(b.GetSize() == 3);
    REQUIRE(b[0] == 9);
    REQUIRE(b[2] == 9);
}

TEST_CASE("Array copy is deep and independent", "[array][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Array<tst::Tracked> a;
        for (int i = 0; i < 5; ++i)
        {
            a.AddInPlace(&led, i);
        }
        Array<tst::Tracked> b = a;
        REQUIRE(b.GetSize() == 5);
        b[0].SetValue(999);
        REQUIRE(a[0].Value() == 0); // independent storage
        REQUIRE(b[0].Value() == 999);
    }
    REQUIRE(led.Live() == 0);
}

TEST_CASE("Array move leaves source empty and valid", "[array][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Array<tst::Tracked> a;
        for (int i = 0; i < 5; ++i)
        {
            a.AddInPlace(&led, i);
        }
        const long ctorsBefore = led.Constructions();
        Array<tst::Tracked> b = std::move(a);
        REQUIRE(led.Constructions() == ctorsBefore); // move steals; no element moves
        REQUIRE(b.GetSize() == 5);
        REQUIRE(a.GetSize() == 0); // NOLINT: intentional moved-from inspection
        REQUIRE(a.GetCapacity() == 0);
        REQUIRE(a.GetData() == nullptr);
        a.Add(tst::Tracked(&led, 42)); // reuse moved-from
        REQUIRE(a.GetSize() == 1);
    }
    REQUIRE(led.Live() == 0);
}

TEST_CASE("Array self copy- and move-assignment are safe", "[array]")
{
    Array<int> v;
    for (int i = 0; i < 10; ++i)
    {
        v.Add(i);
    }
#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wself-assign-overloaded"
#    pragma clang diagnostic ignored "-Wself-move"
#endif
    v = v; // NOLINT: exercising the self-copy-assign guard on purpose
    REQUIRE(v.GetSize() == 10);
    REQUIRE(v[5] == 5);
    v = std::move(v); // NOLINT: exercising the self-move-assign guard on purpose
#if defined(__clang__)
#    pragma clang diagnostic pop
#endif
    REQUIRE(v.GetSize() == 10);
    REQUIRE(v[9] == 9);
}

TEST_CASE("Array move-only element type (UniquePtr-like)", "[array][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Array<tst::MoveOnly> v;
        for (int i = 0; i < 50; ++i)
        {
            v.AddInPlace(&led, i);
        }
        REQUIRE(v.GetSize() == 50);
        REQUIRE(v[10].Value() == 10);
        Array<tst::MoveOnly> moved = std::move(v);
        REQUIRE(moved.GetSize() == 50);
    }
    REQUIRE(led.Live() == 0);
}

TEST_CASE("Array pop_back", "[array][lifetime]")
{
    tst::LifetimeLedger led;
    Array<tst::Tracked> v;
    v.AddInPlace(&led, 1);
    v.AddInPlace(&led, 2);
    v.RemoveLast();
    REQUIRE(v.GetSize() == 1);
    REQUIRE(v.GetLast().Value() == 1);
    v.RemoveLast();
    REQUIRE(v.IsEmpty());
    REQUIRE(led.Live() == 0);
}

TEST_CASE("Array insert/erase ordered", "[array]")
{
    Array<int> v;
    for (int i = 0; i < 5; ++i)
    {
        v.Add(i * 10); // 0 10 20 30 40
    }
    v.InsertAt(2, 99); // 0 10 99 20 30 40
    REQUIRE(v.GetSize() == 6);
    REQUIRE(v[2] == 99);
    REQUIRE(v[3] == 20);
    REQUIRE(v[5] == 40);

    v.RemoveAt(2); // back to 0 10 20 30 40
    REQUIRE(v.GetSize() == 5);
    REQUIRE(v[2] == 20);

    v.InsertAt(0, -1); // -1 0 10 20 30 40
    REQUIRE(v.GetFirst() == -1);
    v.InsertAt(v.GetSize(), 100); // append via insert-at-end
    REQUIRE(v.GetLast() == 100);
}

TEST_CASE("Array insert/erase with lifetime tracking", "[array][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Array<tst::Tracked> v;
        for (int i = 0; i < 6; ++i)
        {
            v.AddInPlace(&led, i);
        }
        v.InsertAtInPlace(3, &led, 100);
        REQUIRE(v[3].Value() == 100);
        REQUIRE(v[4].Value() == 3);
        v.RemoveAt(0);
        REQUIRE(v[0].Value() == 1);
        v.RemoveRange(1, 3);
        REQUIRE(v.GetSize() == 4);
    }
    REQUIRE(led.Live() == 0);
}

TEST_CASE("Array erase range full and partial", "[array]")
{
    Array<int> v;
    for (int i = 0; i < 10; ++i)
    {
        v.Add(i);
    }
    v.RemoveRange(2, 5); // remove 2,3,4
    REQUIRE(v.GetSize() == 7);
    REQUIRE(v[2] == 5);
    v.RemoveRange(0, v.GetSize()); // clear via range
    REQUIRE(v.IsEmpty());
}

TEST_CASE("Array EraseUnordered is O(1) swap-with-last", "[array]")
{
    Array<int> v;
    for (int i = 0; i < 5; ++i)
    {
        v.Add(i); // 0 1 2 3 4
    }
    v.RemoveAtSwap(1); // last (4) moves into slot 1 -> 0 4 2 3
    REQUIRE(v.GetSize() == 4);
    REQUIRE(v[1] == 4);
    v.RemoveAtSwap(3); // erase last element directly
    REQUIRE(v.GetSize() == 3);
    REQUIRE(v[0] == 0);
}

TEST_CASE("Array append from span (bulk)", "[array][interop]")
{
    Array<int> v;
    int src[] = {1, 2, 3, 4, 5};
    v.AddRange(std::span<const int>(src, 5));
    REQUIRE(v.GetSize() == 5);
    v.AddRange(std::span<const int>(src, 5));
    REQUIRE(v.GetSize() == 10);
    REQUIRE(v[9] == 5);
    // Append empty is a no-op.
    v.AddRange(std::span<const int>());
    REQUIRE(v.GetSize() == 10);
}

TEST_CASE("Array From factory", "[array]")
{
    int src[] = {9, 8, 7};
    Array<int> v = Array<int>::FromRange(std::span<const int>(src, 3));
    REQUIRE(v.GetSize() == 3);
    REQUIRE(v[0] == 9);
    REQUIRE(v[2] == 7);
}

TEST_CASE("Array swap", "[array]")
{
    Array<int> a;
    a.Add(1);
    a.Add(2);
    Array<int> b;
    b.Add(9);
    a.Swap(b);
    REQUIRE(a.GetSize() == 1);
    REQUIRE(a[0] == 9);
    REQUIRE(b.GetSize() == 2);
    REQUIRE(b[1] == 2);
}

TEST_CASE("Array shrink_to_fit", "[array]")
{
    Array<int> v;
    v.EnsureCapacity(100);
    v.Add(1);
    v.Add(2);
    REQUIRE(v.GetCapacity() >= 100);
    v.TrimCapacity();
    REQUIRE(v.GetCapacity() == 2);
    REQUIRE(v.GetSize() == 2);
    REQUIRE(v[0] == 1);
    v.Clear();
    v.TrimCapacity();
    REQUIRE(v.GetCapacity() == 0);
    REQUIRE(v.GetData() == nullptr);
}

TEST_CASE("Array std interop: ranges, algorithms, span, pointer+count", "[array][interop]")
{
    Array<int> v;
    for (int i = 5; i >= 1; --i)
    {
        v.Add(i);
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
    REQUIRE(s.data() == v.GetData());
}

TEST_CASE("Array over-aligned elements", "[array][alignment]")
{
    Array<tst::OverAligned> v;
    for (int i = 0; i < 20; ++i)
    {
        v.AddInPlace(i);
    }
    REQUIRE(reinterpret_cast<std::uintptr_t>(v.GetData()) % 128 == 0);
    REQUIRE(v[19].Value == 19);
}

TEST_CASE("Array empty element type", "[array]")
{
    Array<tst::Empty> v;
    for (int i = 0; i < 10; ++i)
    {
        v.AddInPlace();
    }
    REQUIRE(v.GetSize() == 10);
    v.RemoveLast();
    REQUIRE(v.GetSize() == 9);
}

TEST_CASE("Array equality", "[array]")
{
    Array<int> a;
    Array<int> b;
    a.Add(1);
    a.Add(2);
    b.Add(1);
    b.Add(2);
    REQUIRE(a == b);
    b.Add(3);
    REQUIRE_FALSE(a == b);
}

TEST_CASE("Array zero and one element edge cases", "[array]")
{
    Array<int> v;
    REQUIRE(v.TryEnsureCapacity(0));
    REQUIRE(v.GetCapacity() == 0);
    v.Add(42);
    REQUIRE(v.GetSize() == 1);
    REQUIRE(v.GetFirst() == v.GetLast());
    v.RemoveLast();
    REQUIRE(v.IsEmpty());
}

TEST_CASE("Array GetMaxSize overflow defense (fallible)", "[array][overflow]")
{
    Array<int> v;
    // A request beyond MaxSize must fail gracefully, not wrap/allocate tiny.
    REQUIRE_FALSE(v.TryEnsureCapacity(Array<int>::GetMaxSize() + 1));
    REQUIRE(v.GetSize() == 0);
    REQUIRE(v.GetCapacity() == 0);
}

// ---------------------------------------------------------------------------
// Regression tests for the self-referential insertion bug found in audit
// (heap-use-after-free / silent wrong value when an argument aliased an element
// and the operation reallocated or shifted). std::vector guarantees these are
// well-defined; Ludus::Array must match.
// ---------------------------------------------------------------------------

TEST_CASE("Array self-referential Add triggering reallocation", "[array][regression][aliasing]")
{
    Array<int> v;
    v.EnsureCapacity(4);
    v.Add(10);
    v.Add(20);
    v.Add(30);
    v.Add(40);   // size == capacity == 4
    v.Add(v[0]); // arg aliases storage that reallocation frees
    REQUIRE(v.GetSize() == 5);
    REQUIRE(v[4] == 10);
    // Also v.GetLast() self-reference across several growths.
    for (int i = 0; i < 100; ++i)
    {
        v.Add(v.GetLast());
    }
    REQUIRE(v.GetLast() == 10);
}

TEST_CASE("Array self-referential AddInPlace triggering reallocation", "[array][regression][aliasing]")
{
    Array<int> v;
    v.EnsureCapacity(2);
    v.Add(7);
    v.Add(8);           // full
    v.AddInPlace(v[1]); // realloc happens; arg aliases v[1]
    REQUIRE(v.GetSize() == 3);
    REQUIRE(v[2] == 8);
}

TEST_CASE("Array self-referential InsertAtAtInPlace without reallocation", "[array][regression][aliasing]")
{
    Array<int> v;
    for (int i = 0; i < 6; ++i)
    {
        v.Add(i * 10); // 0 10 20 30 40 50
    }
    const int expected = v[5];  // 50
    v.InsertAtInPlace(1, v[5]); // v[5] is shifted during the op
    REQUIRE(v.GetSize() == 7);
    REQUIRE(v[1] == expected);
    REQUIRE(v[2] == 10); // original v[1] shifted right
}

TEST_CASE("Array self-referential InsertAtAtInPlace triggering reallocation", "[array][regression][aliasing]")
{
    Array<int> v;
    v.EnsureCapacity(4);
    v.Add(1);
    v.Add(2);
    v.Add(3);
    v.Add(4); // full
    const int expected = v[3];
    v.InsertAtInPlace(1, v[3]); // realloc + insert of a self-reference
    REQUIRE(v.GetSize() == 5);
    REQUIRE(v[1] == expected);
}

TEST_CASE("Array self-referential InsertAt of a shifted element", "[array][regression][aliasing]")
{
    Array<int> v;
    v.EnsureCapacity(8);
    for (int i = 0; i < 5; ++i)
    {
        v.Add(i); // 0 1 2 3 4
    }
    v.InsertAt(1, v[2]); // v[2] (==2) is shifted right during Insert
    REQUIRE(v[1] == 2);
}

TEST_CASE("Array self-referential Add of non-trivial type across growth", "[array][regression][aliasing][lifetime]")
{
    tst::LifetimeLedger led;
    {
        Array<tst::Tracked> v;
        v.EnsureCapacity(2);
        v.AddInPlace(&led, 1);
        v.AddInPlace(&led, 2); // full
        // Self-referential copy of an existing element while reallocating. The
        // Tracked canary + ASan would catch a use-after-free of the old block.
        v.Add(v[0]);
        REQUIRE(v.GetSize() == 3);
        REQUIRE(v[2].Value() == 1);
        // Force several more growths with self-references.
        for (int i = 0; i < 50; ++i)
        {
            v.Add(v.GetLast());
        }
        REQUIRE(v.GetLast().Value() == 1);
    }
    REQUIRE(led.Live() == 0); // no leak / no double-destroy
}

TEST_CASE("Array self-referential AddInPlace of move-eligible relocatable across growth",
          "[array][regression][aliasing]")
{
    // Relocatable opts into memcpy relocation; the new element must still be
    // constructed from the aliased source before the old block is freed.
    tst::LifetimeLedger led;
    Array<tst::Relocatable> v;
    v.EnsureCapacity(2);
    v.AddInPlace(&led, 100);
    v.AddInPlace(&led, 200); // full
    v.Add(v[1]);             // copy of v[1] while reallocating (memcpy path)
    REQUIRE(v.GetSize() == 3);
    REQUIRE(v[2].Value == 200);
}
