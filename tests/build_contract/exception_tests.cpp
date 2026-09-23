#include <catch2/catch_test_macros.hpp>

#if !defined(__cpp_exceptions)
#    error "Catch2 test targets must enable exceptions"
#endif

TEST_CASE("test-only exception policy permits unwinding", "[build-contract]")
{
    bool destroyed = false;
    struct Cleanup
    {
        bool& Destroyed;
        ~Cleanup()
        {
            Destroyed = true;
        }
    };

    const auto fail = [&destroyed] {
        const Cleanup cleanup{destroyed};
        throw 7;
    };
    CHECK_THROWS_AS(fail(), int);
    CHECK(destroyed);
}
