#include <ludus/foundation/containers/sort.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace ludus::foundation;

TEST_CASE("Owned sort handles adversarial order, duplicates and empty input", "[containers][sort]")
{
    const auto less = [](int32 a, int32 b) noexcept { return a < b; };
    Sort<int32>(nullptr, 0, less);
    int32 actual[257];
    int32 expected[257];
    for (usize count = 0; count <= 257; ++count)
    {
        for (int32 pattern = 0; pattern < 4; ++pattern)
        {
            for (usize i = 0; i < count; ++i)
            {
                const int32 value = static_cast<int32>(i);
                actual[i] = pattern == 0 ? value : pattern == 1 ? -value : pattern == 2 ? value % 7 : 0;
                expected[i] = actual[i];
            }
            Sort(actual, count, less);
            std::sort(expected, expected + count);
            for (usize i = 0; i < count; ++i)
            {
                REQUIRE(actual[i] == expected[i]);
            }
        }
    }
}

TEST_CASE("Owned sort supports move-only elements and reverse comparison", "[containers][sort]")
{
    struct Value
    {
        int32 Key;
        explicit Value(int32 key) noexcept : Key(key) {}
        Value(const Value&) = delete;
        Value& operator=(const Value&) = delete;
        Value(Value&& other) noexcept : Key(other.Key)
        {
            other.Key = -1;
        }
        Value& operator=(Value&& other) noexcept
        {
            Key = other.Key;
            other.Key = -1;
            return *this;
        }
    };
    Value values[] = {Value(1), Value(4), Value(2), Value(3)};
    Sort(values, 4, [](const Value& a, const Value& b) noexcept { return a.Key > b.Key; });
    for (usize i = 0; i < 4; ++i)
    {
        REQUIRE(values[i].Key == static_cast<int32>(4 - i));
    }
}
