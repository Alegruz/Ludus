// Unit tests for Ludus::Array<T, N>.

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

TEST_CASE("Array layout matches a raw C array", "[array]")
{
    STATIC_REQUIRE(sizeof(Array<int, 4>) == sizeof(int[4]));
    STATIC_REQUIRE(alignof(Array<int, 4>) == alignof(int[4]));
    STATIC_REQUIRE(std::is_trivially_copyable_v<Array<int, 4>>);
    STATIC_REQUIRE(std::is_trivially_destructible_v<Array<int, 4>>);
    STATIC_REQUIRE(alignof(Array<tst::OverAligned, 2>) == 128);
}

TEST_CASE("Array aggregate init and access", "[array]")
{
    Array<int, 3> a{1, 2, 3};
    REQUIRE(a.Size() == 3);
    REQUIRE(a.Capacity() == 3);
    REQUIRE_FALSE(a.Empty());
    REQUIRE(a[0] == 1);
    REQUIRE(a[2] == 3);
    REQUIRE(a.Front() == 1);
    REQUIRE(a.Back() == 3);
    a[1] = 20;
    REQUIRE(a[1] == 20);
}

TEST_CASE("Array constexpr behavior", "[array]")
{
    constexpr Array<int, 4> a{5, 6, 7, 8};
    STATIC_REQUIRE(a.Size() == 4);
    STATIC_REQUIRE(a[0] == 5);
    STATIC_REQUIRE(a.Back() == 8);
    STATIC_REQUIRE(a.Front() == 5);
    constexpr int sum = [] {
        Array<int, 3> v{1, 2, 3};
        int s = 0;
        for (int x : v)
        {
            s += x;
        }
        return s;
    }();
    STATIC_REQUIRE(sum == 6);
}

TEST_CASE("Array zero-size handling", "[array]")
{
    Array<int, 0> a{};
    REQUIRE(a.Size() == 0);
    REQUIRE(a.Empty());
    REQUIRE(a.begin() == a.end());
    std::span<int> s = a;
    REQUIRE(s.empty());
    STATIC_REQUIRE(sizeof(Array<int, 0>) >= 1); // empty class still has size >= 1
}

TEST_CASE("Array iterators and std algorithms", "[array][interop]")
{
    Array<int, 5> a{5, 3, 1, 4, 2};
    std::sort(a.begin(), a.end());
    REQUIRE(a[0] == 1);
    REQUIRE(a[4] == 5);
    const int total = std::accumulate(a.begin(), a.end(), 0);
    REQUIRE(total == 15);
    REQUIRE(std::ranges::find(a, 4) != a.end());
}

TEST_CASE("Array span interop and Data", "[array][interop]")
{
    Array<int, 3> a{7, 8, 9};
    int* d = a.Data();
    REQUIRE(d == &a[0]);
    std::span<int> ms = a.AsSpan();
    REQUIRE(ms.size() == 3);
    ms[0] = 70;
    REQUIRE(a[0] == 70);

    const Array<int, 3>& cref = a;
    std::span<const int> cs = cref;
    REQUIRE(cs.size() == 3);
    REQUIRE(cs[2] == 9);
}

TEST_CASE("Array Fill and Swap", "[array]")
{
    Array<int, 4> a{0, 0, 0, 0};
    a.Fill(9);
    REQUIRE(a[0] == 9);
    REQUIRE(a[3] == 9);

    Array<int, 3> x{1, 2, 3};
    Array<int, 3> y{4, 5, 6};
    x.Swap(y);
    REQUIRE(x[0] == 4);
    REQUIRE(y[0] == 1);
}

TEST_CASE("Array comparison operators", "[array]")
{
    Array<int, 3> a{1, 2, 3};
    Array<int, 3> b{1, 2, 3};
    Array<int, 3> c{1, 2, 4};
    REQUIRE(a == b);
    REQUIRE(a != c);
    REQUIRE(a < c);
}

TEST_CASE("Array over-aligned element", "[array][alignment]")
{
    Array<tst::OverAligned, 3> a{};
    REQUIRE(reinterpret_cast<std::uintptr_t>(a.Data()) % 128 == 0);
    a[1].Value = 42;
    REQUIRE(a[1].Value == 42);
}

TEST_CASE("Array deduction guide", "[array]")
{
    // CTAD works on the class template (the alias cannot deduce).
    ludus::foundation::core::Array deduced{1, 2, 3, 4};
    STATIC_REQUIRE(std::is_same_v<decltype(deduced), ludus::foundation::core::Array<int, 4>>);
    REQUIRE(deduced.Size() == 4);
}
